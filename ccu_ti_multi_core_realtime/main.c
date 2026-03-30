/**
 * @file main.c
 * @brief Core 1 (NoRTOS) - CAN Real-time Control
 *
 * CCU Multicore Project - Core 1:
 * - NoRTOS (Bare-metal)
 * - 8x MCAN (CAN 0-7)
 * - 1000Hz cyclic loop for motor control
 * - IPC Producer/Consumer
 *
 * @author CCU Multicore Project
 * @date 2026-03-18
 */

#include <stdlib.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/HwiP.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/TimerP.h>
#include <drivers/gpio.h>
#include <drivers/ipc_notify.h>
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"
#include "../gateway_shared.h"
#include "can_interface.h"
#include "motor_mapping.h"
#include "../common/motor_config_types.h"
#include "imu/imu_interface_isr.h"
#include "imu/imu_protocol_handler.h"

/* ==========================================================================
 * DEBUG COUNTERS (QA TRACE)
 * ==========================================================================
 * Debug counters placed in shared memory for JTAG visibility.
 * These are defined here in main.c to ensure linker includes them.
 */
volatile uint32_t dbg_ipc_send_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;
volatile uint32_t dbg_ipc_register_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;

/* Core ID definitions from CSL */
#ifndef CSL_CORE_ID_R5FSS0_0
#define CSL_CORE_ID_R5FSS0_0         (0U)
#endif
#ifndef CSL_CORE_ID_R5FSS0_1
#define CSL_CORE_ID_R5FSS0_1         (1U)
#endif

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define SYSTEM_CORE_CLOCK_HZ      800000000  /* 800 MHz */
#define LOOP_FREQUENCY_HZ         1000        /* 1000 Hz */
#define LOOP_PERIOD_US           (1000000 / LOOP_FREQUENCY_HZ)  /* 1000 us */

/* [QA TRACE] Debug GPIO pins for Core1 instrumentation */
#define DEBUG_GPIO_CORE1_HEARTBEAT_BASE_ADDR  (CSL_GPIO3_U_BASE)
#define DEBUG_GPIO_CORE1_HEARTBEAT_PIN        (40U)  /* GPIO3 PA0 = Core1 Heartbeat */
#define DEBUG_GPIO_TIMER_ISR_BASE_ADDR        (CSL_GPIO3_U_BASE)
#define DEBUG_GPIO_TIMER_ISR_PIN              (41U)  /* GPIO3 PA1 = Timer ISR indicator [QA TRACE T016] */
#define DEBUG_GPIO_CAN_RX_ISR_BASE_ADDR       (CSL_GPIO3_U_BASE)
#define DEBUG_GPIO_CAN_RX_ISR_PIN             (42U)  /* GPIO3 PA2 = CAN RX ISR indicator [QA TRACE T021] */
#define DEBUG_GPIO_IMU_UART_ISR_BASE_ADDR     (CSL_GPIO3_U_BASE)
#define DEBUG_GPIO_IMU_UART_ISR_PIN           (43U)  /* GPIO3 PA3 = IMU UART ISR indicator [QA TRACE T022] */

/*==============================================================================
 * GLOBAL VARIABLES
 *============================================================================*/

/* 1000Hz Timer */
/* BUG B006 FIX: Changed from uint64_t to uint32_t for atomic access on 32-bit ARM.
 * 32-bit counter provides ~68 years before overflow @ 1kHz (4.29 billion cycles).
 * On ARM Cortex-R5F, 64-bit read/write is NOT atomic - can cause torn reads
 * when ISR increments while main loop reads. Using 32-bit ensures atomicity
 * without requiring critical sections or IRQ disable.
 */
static volatile uint32_t g_cycle_count = 0;
static volatile bool g_timer_expired = false;

/* Motor command buffer from shared memory */
/* BUG B005 FIX: Double buffering to prevent data race on motor commands
 * Problem: g_motor_commands array written by IPC ISR (via ringbuf) and read by main loop
 *          Without synchronization, main loop could read partially updated command data
 *          causing erratic motor behavior (e.g., position from old command, velocity from new)
 * Solution: Double buffering with atomic flag
 * - g_motor_commands_buffer: Written by IPC ISR (via ringbuf receive)
 * - g_motor_commands_working: Used by main loop for CAN transmission
 * - g_buffer_ready: Flag indicating new data available in buffer
 * - Atomic swap: Main loop copies buffer to working when ready flag is set
 * - Ensures main loop always reads consistent command data (no torn reads)
 */
static motor_cmd_ipc_t g_motor_commands_buffer[GATEWAY_NUM_MOTORS];
static motor_cmd_ipc_t g_motor_commands_working[GATEWAY_NUM_MOTORS];
static volatile bool g_buffer_ready = false;  /* Flag for buffer swap */
/* BUG B002 FIX: Race condition on g_commands_ready flag
 * Problem: Flag set in IPC ISR (line 243) and read/cleared in main loop (line 604)
 *          No memory barrier - can miss updates or read torn data on ARM Cortex-R5F
 * Solution: Add memory barrier after setting flag in IPC ISR
 * - DMB (Data Memory Barrier) ensures write is visible to main loop immediately
 * - Main loop read doesn't need barrier since it's single-reader, single-writer pattern
 * - Clear operation in main loop is safe because only main loop writes false
 */
static volatile bool g_commands_ready = false;

/* Motor state buffer for shared memory */
static motor_state_ipc_t g_motor_states[GATEWAY_NUM_MOTORS];

/* Heartbeat counter */
static volatile uint32_t g_heartbeat_count = 0;

/* IPC event counter - tracks messages from Core 0 */
static volatile uint32_t g_ipc_event_count = 0;

/* Ring buffer initialization status flag
 * [BUG B013 FIX] Tracks whether ring buffer has been successfully initialized
 * Prevents calling gateway_ringbuf_core1_receive() before initialization completes
 * which could cause undefined behavior or read from uninitialized memory
 */
static volatile bool g_ringbuf_initialized = false;

/* CAN frame buffers for TX - allocated globally to avoid stack overflow in 1000Hz loop
 * [BUG FIX B003] Moved from transmit_can_frames() function scope to file scope
 * Size: 8 buses × 23 motors = 184 frames × 16 bytes = 2,944 bytes
 * Previously allocated on stack in 1000Hz loop, risking stack overflow on bare-metal core
 */
static can_frame_t g_frame_buffers[NUM_CAN_BUSES][GATEWAY_NUM_MOTORS];

/*==============================================================================
 * FORWARD DECLARATIONS
 *============================================================================*/

static void main_loop(void);
void timerISR(void *args);  /* Non-static for syscfg timer callback */
static void ipc_notify_callback_fxn(uint32_t remoteCoreId, uint16_t localClientId,
                                     uint32_t msgValue, int32_t crcStatus, void *args);
static int32_t init_1000hz_timer(void);
static void process_can_rx(uint8_t bus_id, const can_frame_t *frame);
static void process_motor_commands(void);
static void transmit_can_frames(void);

/* [QA TRACE] Debug GPIO helper functions */
static void debug_gpio_init_core1(void);
static inline void debug_gpio_toggle(uint32_t baseAddr, uint32_t pin);

/*==============================================================================
 * DEBUG GPIO HELPER FUNCTIONS
 *============================================================================*/

/**
 * @brief Initialize debug GPIO pins for Core1 instrumentation
 *
 * [QA TRACE] Configure GPIO PA0 (heartbeat) and PA1 (timer ISR) as outputs
 * MUST be called before any trace points
 */
static void debug_gpio_init_core1(void)
{
    uint32_t baseAddr;

    /* Configure PA0 as output (heartbeat) */
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(DEBUG_GPIO_CORE1_HEARTBEAT_BASE_ADDR);
    GPIO_setDirMode(baseAddr, DEBUG_GPIO_CORE1_HEARTBEAT_PIN, GPIO_DIRECTION_OUTPUT);
    GPIO_pinWriteLow(baseAddr, DEBUG_GPIO_CORE1_HEARTBEAT_PIN);

    /* Configure PA1 as output (timer ISR indicator) [QA TRACE T016] */
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(DEBUG_GPIO_TIMER_ISR_BASE_ADDR);
    GPIO_setDirMode(baseAddr, DEBUG_GPIO_TIMER_ISR_PIN, GPIO_DIRECTION_OUTPUT);
    GPIO_pinWriteLow(baseAddr, DEBUG_GPIO_TIMER_ISR_PIN);

    /* Configure PA2 as output (CAN RX ISR indicator) [QA TRACE T021] */
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(DEBUG_GPIO_CAN_RX_ISR_BASE_ADDR);
    GPIO_setDirMode(baseAddr, DEBUG_GPIO_CAN_RX_ISR_PIN, GPIO_DIRECTION_OUTPUT);
    GPIO_pinWriteLow(baseAddr, DEBUG_GPIO_CAN_RX_ISR_PIN);

    /* Configure PA3 as output (IMU UART ISR indicator) [QA TRACE T022] */
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(DEBUG_GPIO_IMU_UART_ISR_BASE_ADDR);
    GPIO_setDirMode(baseAddr, DEBUG_GPIO_IMU_UART_ISR_PIN, GPIO_DIRECTION_OUTPUT);
    GPIO_pinWriteLow(baseAddr, DEBUG_GPIO_IMU_UART_ISR_PIN);
}

/**
 * @brief Toggle GPIO pin (fast, no malloc, no OS call)
 *
 * [QA TRACE] Safe for bare-metal and ISR context
 * Minimal overhead (~50ns per toggle)
 *
 * Uses TI SDK GPIO API: GPIO_pinRead, GPIO_pinWriteHigh, GPIO_pinWriteLow
 */
static inline void debug_gpio_toggle(uint32_t baseAddr, uint32_t pin)
{
    /* Toggle using TI SDK GPIO API */
    if (GPIO_pinRead(baseAddr, pin)) {
        GPIO_pinWriteLow(baseAddr, pin);
    } else {
        GPIO_pinWriteHigh(baseAddr, pin);
    }
}

/*==============================================================================
 * TIMER ISR (1000Hz)
 *============================================================================*/

/**
 * @brief 1000Hz Timer ISR
 *
 * Triggers every 1ms to process motor commands and transmit CAN frames
 * NOTE: Function name must match SysConfig timerCallback setting
 * NOTE: Must be non-static for syscfg to find it
 * [QA TRACE T016] GPIO PA1 toggle on ISR entry/exit for timing measurement
 */
void timerISR(void *args)
{
    (void)args;

    /* [QA TRACE T016] GPIO PA1 toggle on ISR entry */
    debug_gpio_toggle(DEBUG_GPIO_TIMER_ISR_BASE_ADDR, DEBUG_GPIO_TIMER_ISR_PIN);

    /* Set flag for main loop processing */
    g_timer_expired = true;
    /* [FIX B001] Memory barrier after flag set - ensures write is visible to main loop
     * On ARM Cortex-R5F, CPU or compiler may reorder operations without barrier.
     * DMB (Data Memory Barrier) ensures all previous writes complete before proceeding. */
    __asm volatile("dmb" ::: "memory");
    g_cycle_count++;

    /* Update heartbeat every cycle */
    gateway_update_heartbeat(1);
    g_heartbeat_count++;
    /* [FIX B016] Memory barrier after heartbeat count increment
     * On ARM Cortex-R5F, increment operation (read-modify-write) is NOT atomic.
     * DMB barrier ensures increment is immediately visible if main loop reads this value.
     * While g_heartbeat_count is currently unused, barrier prevents potential issues
     * if it's used in future for debugging or monitoring. */
    __asm volatile("dmb" ::: "memory");

    /* [QA TRACE T016] GPIO PA1 toggle on ISR exit */
    debug_gpio_toggle(DEBUG_GPIO_TIMER_ISR_BASE_ADDR, DEBUG_GPIO_TIMER_ISR_PIN);
}

/*==============================================================================
 * IPC CALLBACK
 *============================================================================*/

/**
 * @brief IPC notification callback
 *
 * Called when Core 0 sends notification
 * Signature matches IpcNotify_FxnCallback
 *
 * IMPORTANT: This runs in ISR context - keep it minimal!
 */
static void ipc_notify_callback_fxn(uint32_t remoteCoreId, uint16_t localClientId,
                                     uint32_t msgValue, int32_t crcStatus, void *args)
{
    (void)crcStatus;
    (void)args;

    /* [QA TRACE T018] Increment shared counter (atomic for single-core variable) */
    g_ipc_event_count++;

    /* [QA TRACE T023] Increment IPC send counter (Core0→Core1) */
    DEBUG_COUNTER_INC(dbg_ipc_send_count);

    /* Call gateway shared memory callback - handles the actual IPC message */
    gateway_core1_ipc_callback(localClientId, (uint16_t)msgValue);

    /* Check for motor commands ready */
    if (remoteCoreId == CSL_CORE_ID_R5FSS0_0 && msgValue == MSG_ETH_DATA_READY) {
        /* Core 0 has written new motor commands */
        /* BUG B002 FIX: Memory barrier after setting flag to ensure visibility to main loop
         * DMB ensures all previous writes complete before flag write becomes visible
         */
        g_commands_ready = true;
        __asm volatile("dmb" ::: "memory");
    }
}

/*==============================================================================
 * CAN RX CALLBACK
 *============================================================================*/

/**
 * @brief CAN RX callback
 *
 * Called from CAN RX ISR when frame is received
 */
static void process_can_rx(uint8_t bus_id, const can_frame_t *frame)
{
    /* Parse motor response from CAN frame */
    if (frame->dlc >= 8) {
        /* Extract motor ID from CAN ID (Robstride protocol) */
        uint8_t motor_id = (frame->can_id >> 8) & 0x7F;

        /* Find motor index using lookup table */
        uint8_t motor_idx = motor_get_index(motor_id, bus_id);

        if (motor_idx < GATEWAY_NUM_MOTORS) {
            /* Parse motor state from CAN data */
            motor_state_ipc_t *state = &g_motor_states[motor_idx];

            state->motor_id = motor_id;
            state->can_bus = bus_id;
            state->pattern = (frame->can_id >> 6) & 0x03;
            state->error_code = (frame->can_id >> 2) & 0x0F;

            /* Extract data bytes */
            uint16_t pos_raw = (frame->data[1] << 8) | frame->data[0];
            uint16_t vel_raw = (frame->data[3] << 8) | frame->data[2];
            int16_t torque_raw = (frame->data[5] << 8) | frame->data[4];
            int16_t temp_raw = (frame->data[7] << 8) | frame->data[6];

            /* Convert to physical units */
            state->position = pos_raw;  /* 0.01 rad */
            state->velocity = (int16_t)vel_raw;  /* 0.01 rad/s */
            state->torque = torque_raw;  /* 0.01 Nm */
            state->temperature = (int16_t)(temp_raw / 10);  /* 0.1 °C */

            /* Write motor state to shared memory */
            gateway_write_motor_states(state, 1);

            /* Notify Core 0 that new motor states are available */
            gateway_notify_states_ready();
        }
    }
}

/*==============================================================================
 * MOTOR COMMAND PROCESSING
 *============================================================================*/

/**
 * @brief Process motor commands from shared memory
 *
 * Called from 1000Hz loop to get commands from Core 0
 */
static void process_motor_commands(void)
{
    /* [BUG B013 FIX] Check ring buffer initialization before receive
     * Problem: If called before initialization, could read from uninitialized memory
     * Solution: Check g_ringbuf_initialized flag set during gateway_ringbuf_core1_init()
     * This prevents undefined behavior if function is called during startup race condition
     */
    if (!g_ringbuf_initialized) {
        /* Ring buffer not ready - skip receive operation
         * This is safe during initialization; Core 0 will retry sending data */
        return;
    }

    /* Read motor commands from ring buffer (populated by Core 0) */
    uint32_t bytes_read = 0;
    int32_t ret = gateway_ringbuf_core1_receive(g_motor_commands_buffer,
                                                 sizeof(g_motor_commands_buffer),
                                                 &bytes_read);

    if (ret == GATEWAY_RINGBUF_OK && bytes_read > 0) {
        uint8_t count = bytes_read / sizeof(motor_cmd_ipc_t);
        /* Debug logging removed for 1kHz performance - use stats instead */
        (void)count;  /* Suppress unused warning */
        /* BUG B005 FIX: Set flag to indicate new data in buffer */
        g_buffer_ready = true;
    }
}

/*==============================================================================
 * CAN FRAME TRANSMISSION
 *============================================================================*/

/**
 * @brief Transmit CAN frames for all motors
 *
 * Called from 1000Hz loop to send commands to all 23 motors
 */
static void transmit_can_frames(void)
{
    /* Group motors by CAN bus for batch transmission */
    can_frame_t* can_frames_ptr[NUM_CAN_BUSES];
    uint16_t frame_count[NUM_CAN_BUSES] = {0};

    /* [BUG FIX B003] Uses global g_frame_buffers instead of local stack allocation
     * Previous: static can_frame_t frame_buffers[NUM_CAN_BUSES][GATEWAY_NUM_MOTORS];
     * This was on stack (2,944 bytes) in 1000Hz loop, risking overflow on bare-metal core
     * Now uses g_frame_buffers allocated at file scope (global/static memory) */
    /* Build CAN frames for each motor */
    for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
        const motor_config_t *config = motor_get_config(i);
        const motor_cmd_ipc_t *cmd = &g_motor_commands_working[i];

        if (config != NULL && config->can_bus < NUM_CAN_BUSES) {
            can_frame_t *frame = &g_frame_buffers[config->can_bus][frame_count[config->can_bus]++];

            /* Build CAN ID (Robstride protocol) */
            frame->can_id = (COMM_TYPE_MOTION_CONTROL << 24) |
                           ((uint32_t)(cmd->torque & 0xFFFF) << 8) |
                           config->motor_id;
            frame->flags = 0x01;  /* Extended ID */

            /* Build CAN data */
            frame->dlc = 8;

            uint16_t pos_int = (uint16_t)(cmd->position & 0xFFFF);
            uint16_t vel_int = (uint16_t)(cmd->velocity & 0xFFFF);
            uint16_t kp_int = (uint16_t)(cmd->kp & 0xFFFF);
            uint16_t kd_int = (uint16_t)(cmd->kd & 0xFFFF);

            frame->data[0] = pos_int & 0xFF;
            frame->data[1] = (pos_int >> 8) & 0xFF;
            frame->data[2] = vel_int & 0xFF;
            frame->data[3] = (vel_int >> 8) & 0xFF;
            frame->data[4] = kp_int & 0xFF;
            frame->data[5] = (kp_int >> 8) & 0xFF;
            frame->data[6] = kd_int & 0xFF;
            frame->data[7] = (kd_int >> 8) & 0xFF;

            can_frames_ptr[config->can_bus] = g_frame_buffers[config->can_bus];
        }
    }

    /* Transmit frames on all CAN buses in parallel */
    for (uint8_t bus = 0; bus < NUM_CAN_BUSES; bus++) {
        if (frame_count[bus] > 0) {
            int32_t sent = CAN_TransmitBatch(bus, can_frames_ptr[bus], frame_count[bus]);

            if (sent > 0) {
                gateway_update_stat(1, 0);  /* Update CAN TX counter */
            }
        }
    }
}

/*==============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * @brief Initialize 1000Hz timer
 */
static int32_t init_1000hz_timer(void)
{
    int32_t status;

    DebugP_log("[Core1] Initializing 1000Hz timer...\r\n");

    /* TimerP_init() is generated by SysConfig in ti_dpl_config.c
     * It sets up the timer hardware and registers the ISR */
    TimerP_init();

    /* [QA TRACE T010] 1000Hz timer initialization done - GPIO PA0 toggle */
    uint32_t debug_gpio_base = (uint32_t)AddrTranslateP_getLocalAddr(DEBUG_GPIO_CORE1_HEARTBEAT_BASE_ADDR);
    debug_gpio_toggle(debug_gpio_base, DEBUG_GPIO_CORE1_HEARTBEAT_PIN);

    /* Start the timer - this enables the interrupt */
    TimerP_start(gTimerBaseAddr[CONFIG_TIMER0]);

    DebugP_log("[Core1] 1000Hz timer started (CONFIG_TIMER0)\r\n");

    return SystemP_SUCCESS;
}

/**
 * @brief Core 1 initialization
 */
static int32_t core1_init(void)
{
    int32_t status;

    DebugP_log("\r\n");
    DebugP_log("========================================\r\n");
    DebugP_log("  Core 1 (NoRTOS) Initialization CCU TI\r\n");
    DebugP_log("========================================\r\n");

    /* Open drivers */
    Drivers_open();

    /* [QA TRACE T009] Drivers_open() call done - GPIO PA0 toggle */
    uint32_t debug_gpio_base = (uint32_t)AddrTranslateP_getLocalAddr(DEBUG_GPIO_CORE1_HEARTBEAT_BASE_ADDR);
    debug_gpio_toggle(debug_gpio_base, DEBUG_GPIO_CORE1_HEARTBEAT_PIN);

    status = Board_driversOpen();
    DebugP_assert(status == SystemP_SUCCESS);

    /* Initialize gateway shared memory */
    status = gateway_core1_init();
    if (status != 0) {
        DebugP_log("[Core1] ERROR: Gateway shared memory init failed!\r\n");
        return -1;
    }

    /* Initialize motor mapping */
    motor_mapping_init_core1();
    DebugP_log("[Core1] Motor mapping initialized\r\n");

    /* Initialize CAN buses */
    CAN_Init();
    DebugP_log("[Core1] CAN buses initialized\r\n");

    /* Register CAN RX callback */
    CAN_RegisterRxCallback(process_can_rx);
    DebugP_log("[Core1] CAN RX callback registered\r\n");

    /* Register IPC callback - BOTH cores must use the SAME client ID */
    DebugP_log("[Core1] Registering IPC callback with client ID=%u\r\n", GATEWAY_IPC_CLIENT_ID);

    /* [QA TRACE T015] IPC register entry - increment shared counter */
    extern volatile uint32_t dbg_ipc_register_count;
    __asm__ volatile(
        "ldr r0, =dbg_ipc_register_count\n\t"
        "ldr r1, [r0]\n\t"
        "add r1, r1, #1\n\t"
        "dmb\n\t"
        "str r1, [r0]\n\t"
        "dmb"
        ::: "r0", "r1", "memory"
    );

    status = IpcNotify_registerClient(GATEWAY_IPC_CLIENT_ID, (IpcNotify_FxnCallback)ipc_notify_callback_fxn, NULL);
    if (status != SystemP_SUCCESS) {
        DebugP_log("[Core1] ERROR: IpcNotify_registerClient failed! status=%d\r\n", status);
    } else {
        DebugP_log("[Core1] IPC callback registered successfully\r\n");
        /* [QA TRACE T015] IPC register done - increment shared counter again */
        __asm__ volatile(
            "ldr r0, =dbg_ipc_register_count\n\t"
            "ldr r1, [r0]\n\t"
            "add r1, r1, #1\n\t"
            "dmb\n\t"
            "str r1, [r0]\n\t"
            "dmb"
            ::: "r0", "r1", "memory"
        );
    }

    /* Wait for Core 0 to be ready */
    DebugP_log("[Core1] Waiting for Core 0 IPC sync...\r\n");
    status = IpcNotify_syncAll(10000);  /* 10 second timeout for debug */
    if (status != SystemP_SUCCESS) {
        DebugP_log("[Core1] WARNING: IpcNotify_syncAll timeout!\r\n");
    }
    
    /* Now wait for shared memory initialization to be complete */
    status = gateway_core1_wait_for_ready();
    if (status != 0) {
        DebugP_log("[Core1] ERROR: Shared memory init failed!\r\n");
        return -1;
    }

#if GATEWAY_USE_LOCKFREE_RINGBUF
    /* Initialize lock-free ring buffers */
    status = gateway_ringbuf_core1_init();
    if (status != 0) {
        DebugP_log("[Core1] WARNING: Lock-free ring buffer init verification failed!\r\n");
        /* [BUG B013 FIX] Do not set g_ringbuf_initialized if init failed */
    } else {
        /* [BUG B013 FIX] Set flag only after successful initialization */
        g_ringbuf_initialized = true;
        DebugP_log("[Core1] Lock-free ring buffers verified and ready\r\n");
    }
#endif

    /* Initialize 1000Hz timer */
    status = init_1000hz_timer();
    if (status != SystemP_SUCCESS) {
        DebugP_log("[Core1] WARNING: Timer init failed, using simulated timing\r\n");
    }

    /* Note: dispatcher_timer removed - not properly integrated (callback never registered) */

    /* Start CAN RX interrupts */
    CAN_StartRxInterrupts();
    DebugP_log("[Core1] CAN RX interrupts started\r\n");

    /* Initialize IMU (YIS320) */
    status = imu_uart_isr_init();
    if (status != 0) {
        DebugP_log("[Core1] WARNING: IMU UART ISR init failed!\r\n");
    } else {
        DebugP_log("[Core1] IMU initialized\r\n");
    }

    status = imu_protocol_manager_init(IMU_TYPE_YIS320);
    if (status != 0) {
        DebugP_log("[Core1] WARNING: IMU protocol handler init failed!\r\n");
    }

    DebugP_log("\r\n========================================\r\n");
    DebugP_log("  Core 1 Init Complete!\r\n");
    DebugP_log("========================================\r\n\r\n");

    return 0;
}

/*==============================================================================
 * MAIN LOOP
 *============================================================================*/

/**
 * @brief Core 1 main loop (1000Hz cyclic)
 *
 * This is the main control loop that runs at 1000Hz:
 * 1. Check timer flag
 * 2. Read motor commands from shared memory
 * 3. Transmit CAN frames to all 23 motors
 * 4. Process CAN RX (ISR context)
 * 5. Write motor states to shared memory
 */
static void main_loop(void)
{
    DebugP_log("[Core1] Entering main loop (1000Hz)...\r\n");

    uint32_t last_heartbeat_log = 0;  /* BUG B006 FIX: Changed to uint32_t to match g_cycle_count */
    volatile uint32_t busy_wait;

    /* [BUG B004 FIX] Timeout protection for busy-wait loop */
    const uint32_t TIMER_TIMEOUT_MAX = 10000;  /* Max iterations before timeout (~10ms @ 1us per loop) */
    volatile uint32_t dbg_timer_timeout_count = 0;  /* Track timeout occurrences */
    volatile uint32_t dbg_last_timer_timeout_error = 0;  /* Last error code */

    while (1) {
        /* Wait for timer flag from ISR */
        uint32_t timeout_count = 0;  /* [BUG B004 FIX] Reset timeout counter each cycle */

        while (!g_timer_expired) {
            /* Busy wait for timer interrupt */
            for (busy_wait = 0; busy_wait < 100; busy_wait++) {
                /* Short wait */
            }

            /* [BUG B004 FIX] Check for timeout - prevents infinite hang if timer ISR fails */
            if (++timeout_count > TIMER_TIMEOUT_MAX) {
                /* TIMEOUT! Timer ISR failed to fire - force recovery */
                g_timer_expired = true;  /* Force exit from busy-wait */
                dbg_timer_timeout_count++;  /* Track timeout occurrences */
                dbg_last_timer_timeout_error = 0xB004;  /* Error code for timer timeout */

                /* Force memory barrier to ensure changes are visible */
                __asm__ volatile("dmb" ::: "memory");

                /* Note: System continues but timing is compromised. Root cause (timer ISR failure)
                 * should be investigated. This timeout prevents complete system hang. */
                break;
            }
        }

        /* Clear timer flag */
        g_timer_expired = false;

        /* === 1000Hz Processing Start === */

        /* 1. Process motor commands from shared memory */
        if (g_commands_ready) {
            process_motor_commands();
            g_commands_ready = false;
        }
        
        /* BUG B005 FIX: Atomic buffer swap - copy buffer to working if ready */
        /* This ensures working buffer contains consistent command data */
        /* Copy is done before clearing flag to prevent race conditions */
        if (g_buffer_ready) {
            /* Copy entire buffer atomically (single array assignment on ARM) */
            for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
                g_motor_commands_working[i] = g_motor_commands_buffer[i];
            }
            /* Clear flag after copy - safe because only main loop clears it */
            g_buffer_ready = false;
        }

        /* 2. Transmit CAN frames to all motors */
        transmit_can_frames();

        /* 3. Periodic heartbeat log every 1000 cycles (1 second) */
        if ((g_cycle_count - last_heartbeat_log) >= 1000) {
            DebugP_log("[Core1] Heartbeat: cycle=%u, ipc_events=%u\r\n",  /* BUG B006 FIX: Changed format %llu to %u */
                       g_cycle_count, g_ipc_event_count);
            last_heartbeat_log = g_cycle_count;
        }

        /* BUG B010 FIX: Process IMU IPC notification in task context */
        /* Check if IMU data is ready and notify Core0 (safe from ISR deadlock) */
        if (imu_uart_process_ipc_notification()) {
            /* IMU data notification sent to Core0 */
        }

        /* === 1000Hz Processing End === */

        /* Check for IPC events (non-blocking) */
        /* IPC events are handled in ISR context via ipc_notify_callback */
    }
}

/*==============================================================================
 * ENTRY POINT
 *============================================================================*/

/**
 * @brief Core 1 main entry point
 */
int main(void)
{
    /* [QA TRACE T006] Initialize debug GPIO first */
    debug_gpio_init_core1();
    uint32_t debug_gpio_base = (uint32_t)AddrTranslateP_getLocalAddr(DEBUG_GPIO_CORE1_HEARTBEAT_BASE_ADDR);

    /* [QA TRACE T006] main() entry - GPIO PA0 toggle */
    debug_gpio_toggle(debug_gpio_base, DEBUG_GPIO_CORE1_HEARTBEAT_PIN);

    /* Initialize SOC and Board */
    System_init();

    /* [QA TRACE T007] System_init() call done - GPIO PA0 toggle */
    debug_gpio_toggle(debug_gpio_base, DEBUG_GPIO_CORE1_HEARTBEAT_PIN);

    Board_init();

    /* [QA TRACE T008] Board_init() call done - GPIO PA0 toggle */
    debug_gpio_toggle(debug_gpio_base, DEBUG_GPIO_CORE1_HEARTBEAT_PIN);

    /* Initialize Core 1 */
    int32_t status = core1_init();
    if (status != 0) {
        DebugP_log("[Core1] ERROR: Initialization failed!\r\n");
        return -1;
    }

    /* Start main loop (never returns) */
    main_loop();

    /* Should never reach here */
    DebugP_log("[Core1] ERROR: Main loop exited unexpectedly!\r\n");

    Board_driversClose();
    /* Drivers_close() not called to keep UART open */

    return 0;
}
