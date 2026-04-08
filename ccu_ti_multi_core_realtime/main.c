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
#include <string.h>
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

/* IMU Debug counters - for tracking IMU UART ISR activity */
volatile uint32_t dbg_imu_uart_isr_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;
volatile uint32_t dbg_imu_rx_byte_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;
volatile uint32_t dbg_imu_frame_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;

/* CAN Debug counters - external references from can_interface.c */
extern volatile uint32_t dbg_can_tx_count;  /* CAN TX frame counter */
extern volatile uint32_t dbg_can_rx_count;  /* CAN RX frame counter */

/* [FIX B040] Core ID definitions - AM263Px has 4 R5F cores:
 *
 * Core ID Mappings:
 * - CSL_CORE_ID_R5FSS0_0 = 0 (Core0 - R5FSS0-0, FreeRTOS, Ethernet) ← Our IPC target!
 * - CSL_CORE_ID_R5FSS0_1 = 1 (Core1 - R5FSS0-1, NOT USED)
 * - CSL_CORE_ID_R5FSS1_0 = 2 (Core2 - R5FSS1-0, NOT USED)
 * - CSL_CORE_ID_R5FSS1_1 = 3 (Core3 - R5FSS1-1, NoRTOS, CAN) ← THIS IS US!
 *
 * This code runs on Core1 (R5FSS1-1, ID=3) and receives IPC from Core0 (R5FSS0-0, ID=0)
 */
#ifndef CSL_CORE_ID_R5FSS0_0
#define CSL_CORE_ID_R5FSS0_0         (0U)  /* Core0 - R5FSS0-0 (FreeRTOS, Ethernet) */
#endif
#ifndef CSL_CORE_ID_R5FSS0_1
#define CSL_CORE_ID_R5FSS0_1         (1U)  /* Core1 - R5FSS0-1 (unused) */
#endif
#ifndef CSL_CORE_ID_R5FSS1_0
#define CSL_CORE_ID_R5FSS1_0         (2U)  /* Core2 - R5FSS1-0 (unused) */
#endif
#ifndef CSL_CORE_ID_R5FSS1_1
#define CSL_CORE_ID_R5FSS1_1         (3U)  /* Core3 - R5FSS1-1 (NoRTOS, CAN) - THIS IS US! */
#endif

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define SYSTEM_CORE_CLOCK_HZ      800000000  /* 800 MHz */
#define LOOP_FREQUENCY_HZ         1000        /* 1000 Hz */
#define LOOP_PERIOD_US           (1000000 / LOOP_FREQUENCY_HZ)  /* 1000 us */

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
/* [FIX B083] Initialize to zeros to prevent uninitialized data causing CAN TX issues
 * Problem: When receiving partial packets (e.g., single motor enable), only received
 * motors are updated. Remaining motors contain random BSS data, causing incorrect
 * CAN frames to be transmitted.
 *
 * Enable all works: All 23 motors updated → no uninitialized data.
 * Enable single fails: Only 1 motor updated → other 22 have random data.
 *
 * Solution: Initialize buffers to zero, safe defaults will be set in init() */
static motor_cmd_ipc_t g_motor_commands_buffer[GATEWAY_NUM_MOTORS] = {0};
static motor_cmd_ipc_t g_motor_commands_working[GATEWAY_NUM_MOTORS] = {0};
static volatile bool g_buffer_ready = false;  /* Flag for buffer swap */
/* [FIX B093] Track which motor positions were updated (bitmask)
 * Problem: After Fix B091, data is scattered to working[motor_id]
 *          TX loop still uses sequential index i=0..loop_limit-1
 *          When enable motor 16: working[16] updated, but TX loop checks working[0]!
 * Solution: Bitmask tracks which positions have new data
 * Bit 0 = motor 0 updated, Bit 16 = motor 16 updated, etc.
 * TX loop iterates through set bits instead of sequential indices */
static uint32_t g_updated_motors_mask = 0;
/* [FIX B086] Track actual motor count received from ring buffer
 * Problem: When single motor command is sent, count=1, but main loop checks available
 *          AFTER process_motor_commands() consumes data → available=0 → count=0
 * Solution: Store count during receive, use it during buffer copy
 * Default: GATEWAY_NUM_MOTORS (23) for startup safety */
static uint8_t g_received_motor_count = GATEWAY_NUM_MOTORS;
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
/* [FIX B059] EXPLICITLY initialize to zeros to prevent random/uninitialized data
 * Static arrays without explicit initializer contain random BSS data
 * This causes "velocity changing randomly" when motors haven't responded yet */
static motor_state_ipc_t g_motor_states[GATEWAY_NUM_MOTORS] = {0};

/* Heartbeat counter */
static volatile uint32_t g_heartbeat_count = 0;

/* IPC event counter - tracks messages from Core 0 */
static volatile uint32_t g_ipc_event_count = 0;

/* Debug: Timer ISR call counter - tracks if timer is running */
static volatile uint32_t g_timer_isr_count = 0;

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
static bool check_motor_commands_available(void);
static uint8_t receive_motor_commands(void);
static void copy_motor_commands_to_working(uint8_t count);
static void transmit_can_frames(void);

/* Debug GPIO helper functions */
/*==============================================================================
 * TIMER ISR (1000Hz)
 *============================================================================*/

/**
 * @brief 1000Hz Timer ISR
 *
 * Triggers every 1ms to process motor commands and transmit CAN frames
 * NOTE: Function name must match SysConfig timerCallback setting
 * NOTE: Must be non-static for syscfg to find it
 */
void timerISR(void *args)
{
    (void)args;

    /* Set flag for main loop processing */
    g_timer_expired = true;
    /* [FIX B001] Memory barrier after flag set - ensures write is visible to main loop
     * On ARM Cortex-R5F, CPU or compiler may reorder operations without barrier.
     * DMB (Data Memory Barrier) ensures all previous writes complete before proceeding. */
    __asm volatile("dmb" ::: "memory");
    g_cycle_count++;
    g_timer_isr_count++;  /* Debug: track timer ISR calls */

    /* Update heartbeat every cycle */
    gateway_update_heartbeat(1);
    g_heartbeat_count++;
    /* [FIX B016] Memory barrier after heartbeat count increment
     * On ARM Cortex-R5F, increment operation (read-modify-write) is NOT atomic.
     * DMB barrier ensures increment is immediately visible if main loop reads this value.
     * While g_heartbeat_count is currently unused, barrier prevents potential issues
     * if it's used in future for debugging or monitoring. */
    __asm volatile("dmb" ::: "memory");
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

    g_ipc_event_count++;

    /* Call gateway shared memory callback - handles the actual IPC message */
    gateway_core1_ipc_callback(localClientId, (uint16_t)msgValue);

    /* Check for motor commands ready */
    if (remoteCoreId == CSL_CORE_ID_R5FSS0_0 && msgValue == MSG_ETH_DATA_READY) {
        /* Core 0 has written new motor commands */
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
 *
 * [B028-B032] Fixed: Correct byte order, bit masks, and scaling conversions
 * Reference: draft/ccu_ti/ccu_xgw_gateway.c:2182-2195
 */
static void process_can_rx(uint8_t bus_id, const can_frame_t *frame)
{
    /* Parse motor response from CAN frame */
    if (frame->dlc >= 8) {
        /* Extract motor ID from CAN ID (Robstride protocol: bits 8-15) */
        uint8_t motor_id = (frame->can_id >> 8) & 0xFF;

        /* Find motor index using lookup table */
        uint8_t motor_idx = motor_get_index(motor_id, bus_id);

        if (motor_idx < GATEWAY_NUM_MOTORS) {
            /* Get motor config for scaling */
            const motor_config_t *config = motor_get_config(motor_idx);
            if (config == NULL) {
                return;  /* Invalid motor config */
            }

            /* Parse motor state from CAN data */
            motor_state_ipc_t *state = &g_motor_states[motor_idx];

            /* [FIX B066] Send array index instead of hardware motor_id
             * PC uses index (0-22) to lookup motors, not hardware CAN ID */
            state->motor_id = motor_idx;
            state->can_bus = bus_id;

            /* [B032] FIX: Extract pattern and error from correct bit positions
             * CAN ID format: [Comm_Type(31:24)] [Pattern(23:22)] [Error(21:16)] [Motor_ID(15:8)]
             * Pattern: bits 22-23 (mask 0xC00000 >> 22)
             * Error: bits 16-21 (mask 0x3F0000 >> 16) */
            state->pattern = (int8_t)((frame->can_id >> 22) & 0x03);
            state->error_code = (uint8_t)((frame->can_id >> 16) & 0x3F);

            /* [B028] FIX: Parse data payload with correct byte order (MSB first - big-endian)
             * Robstride protocol: [Pos_H, Pos_L, Vel_H, Vel_L, Torque_H, Torque_L, Temp_H, Temp_L]
             * Reference: draft/ccu_ti/ccu_xgw_gateway.c:2186-2189 */
            uint16_t pos_raw = (frame->data[0] << 8) | frame->data[1];
            uint16_t vel_raw = (frame->data[2] << 8) | frame->data[3];
            uint16_t torque_raw = (frame->data[4] << 8) | frame->data[5];
            uint16_t temp_raw = (frame->data[6] << 8) | frame->data[7];

            /* [DEBUG B058] Log raw CAN data to verify parsing - FIRST motor ONLY
             * This helps diagnose if position=65535 is from CAN data or conversion error */
            static volatile bool raw_can_logged = false;
            if (!raw_can_logged) {
                DebugP_log("[Core1] CAN RAW: id=%u, bytes=[%02X %02X %02X %02X %02X %02X %02X %02X], pos_raw=%u, vel_raw=%u\r\n",
                           state->motor_id,
                           frame->data[0], frame->data[1], frame->data[2], frame->data[3],
                           frame->data[4], frame->data[5], frame->data[6], frame->data[7],
                           pos_raw, vel_raw);
                raw_can_logged = true;
            }

            /* [B031] FIX: Convert to physical units using uint16_to_float
             * Reference: draft/ccu_ti/ccu_xgw_gateway.c:2192-2195
             * Apply direction multiplier for position and velocity */
            float pos_float = uint16_to_float(pos_raw, config->limits.p_min, config->limits.p_max, 16) * config->direction;
            float vel_float = uint16_to_float(vel_raw, config->limits.v_min, config->limits.v_max, 16) * config->direction;
            float torque_float = uint16_to_float(torque_raw, config->limits.t_min, config->limits.t_max, 16);

            /* [B030] FIX: Use float division for temperature */
            float temp_float = temp_raw / 10.0f;

            /* [FIX B064] Store float directly - no unit conversion needed */
            state->position = pos_float;
            state->velocity = vel_float;
            state->torque = torque_float;
            state->temperature = temp_float;

            /* [DEBUG B061] Verify conversion - log first time only */
            static bool conversion_logged = false;
            if (!conversion_logged) {
                DebugP_log("[Core1] CONVERT: idx=%u (hw_id=%u), pos=%.3f rad, vel=%.3f rad/s, trq=%.3f Nm, temp=%.1f C\r\n",
                           state->motor_id, motor_id, state->position, state->velocity, state->torque, state->temperature);
                conversion_logged = true;
            }

            /* [FIX B053] Update local buffer only - do NOT write to shared memory here!
             * Problem: Calling gateway_write_motor_states() here with count=1 causes:
             * 1. Only motors[0] to be written (for loop only writes first element)
             * 2. ready_flag=1 set 23 times (once per motor response)
             * 3. Core0 reads incomplete data (only last motor received)
             *
             * Solution: Only update local g_motor_states buffer here.
             * Main loop will periodically write ALL 23 motors to shared memory. */

            /* [FIXME B053] OLD CODE - REMOVED:
            gateway_write_motor_states(state, 1);
            gateway_notify_states_ready();
            */
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
 *
 * [FIX B086] Stores received motor count in static variable for use during copy.
 * This is needed because we calculate count from bytes_read here, but use it
 * later when copying from buffer to working buffer.
 */
/**
 * @brief Check if motor commands are available from ring buffer
 *
 * [FIX B096] Separate check from receive - allows draining multiple packets
 *
 * @return true if data available, false otherwise
 */
static bool check_motor_commands_available(void)
{
    if (!g_ringbuf_initialized) {
        return false;
    }

    uint32_t available = gateway_ringbuf_core1_get_available();
    return (available >= sizeof(motor_cmd_ipc_t));
}

/**
 * @brief Receive motor commands from ring buffer
 *
 * [FIX B096] Renamed from process_motor_commands - now only receives
 * Copy to working buffer is done separately in main loop
 *
 * @return Number of motors received, or 0 if no data
 */
static uint8_t receive_motor_commands(void)
{
    /* [BUG B013 FIX] Check ring buffer initialization before receive
     * Problem: If called before initialization, could read from uninitialized memory
     * Solution: Check g_ringbuf_initialized flag set during gateway_ringbuf_core1_init()
     * This prevents undefined behavior if function is called during startup race condition
     */
    if (!g_ringbuf_initialized) {
        /* Ring buffer not ready - skip receive operation
         * This is safe during initialization; Core 0 will retry sending data */
        DebugP_log("[Core1] ERROR: receive_motor_commands called but ringbuf NOT initialized!\r\n");
        return 0;
    }

    /* [ROOT CAUSE FIX] Read only available bytes, NOT entire buffer!
     * Problem: Old code tried to read sizeof(g_motor_commands_buffer) = 736 bytes
     * Ring buffer only had 32 bytes (1 motor) → available < size → return EMPTY!
     * Solution: Check available first, then read only what's available
     * Reference: draft/ccu_ti uses ping-pong buffer which reads fixed size,
     * but ring buffer can have partial data */

    /* Check how many bytes are available */
    uint32_t available = gateway_ringbuf_core1_get_available();
    if (available < sizeof(motor_cmd_ipc_t)) {
        /* Not enough data for even 1 motor command */
        return 0;
    }

    /* Calculate how many motors we can read (safely) */
    uint8_t max_motors = sizeof(g_motor_commands_buffer) / sizeof(motor_cmd_ipc_t);
    uint32_t bytes_to_read = available;
    uint32_t max_bytes = max_motors * sizeof(motor_cmd_ipc_t);

    /* Don't read more than buffer size or more than available */
    if (bytes_to_read > max_bytes) {
        bytes_to_read = max_bytes;
    }

    /* Read motor commands from ring buffer (populated by Core 0) */
    uint32_t bytes_read = 0;
    int32_t ret = gateway_ringbuf_core1_receive(g_motor_commands_buffer,
                                                 bytes_to_read,  // ← Read ONLY available bytes
                                                 &bytes_read);

    if (ret == GATEWAY_RINGBUF_OK && bytes_read > 0) {
        uint8_t count = bytes_read / sizeof(motor_cmd_ipc_t);
        /* [DEBUG B095] Log received motors */
        for (uint8_t i = 0; i < count && i < 5; i++) {  /* Log first 5 motors */
            DebugP_log("[Core1] RX: motor[%u].id=%u, mode=%u\r\n",
                       i, g_motor_commands_buffer[i].motor_id, g_motor_commands_buffer[i].mode);
        }
        return count;
    } else if (ret == GATEWAY_RINGBUF_EMPTY) {
        /* No data available */
        return 0;
    } else {
        /* Error */
        DebugP_log("[Core1] RX ERROR: ret=%d\r\n", ret);
        return 0;
    }
}

/**
 * @brief Copy motor commands from buffer to working buffer
 *
 * [FIX B096] NEW function - copies buffer to working and updates mask
 * Called after receive_motor_commands() to apply new commands
 *
 * @param count Number of motors in buffer to copy
 */
static void copy_motor_commands_to_working(uint8_t count)
{
    if (count == 0 || count > GATEWAY_NUM_MOTORS) {
        return;
    }

    /* [FIX B091/B093] Copy based on motor_id, track updated positions */
    for (uint8_t i = 0; i < count; i++) {
        uint8_t motor_index = g_motor_commands_buffer[i].motor_id;
        if (motor_index < GATEWAY_NUM_MOTORS) {
            g_motor_commands_working[motor_index] = g_motor_commands_buffer[i];
            g_updated_motors_mask |= (1U << motor_index);  /* Mark as updated */
            DebugP_log("[Core1] Copy: motor[%u]=id%u, mode%u, mask=0x%08X\r\n",
                       i, motor_index, g_motor_commands_working[motor_index].mode, g_updated_motors_mask);
        }
    }
}

/*==============================================================================
 * CAN FRAME TRANSMISSION
 *============================================================================*/

/**
 * @brief Transmit CAN frames for all motors
 *
 * Called from 1000Hz loop to send commands to all 23 motors
 *
 * [STUB S002] Check emergency stop flag before transmitting
 */
static void transmit_can_frames(void)
{
    /* [DEBUG] Static counter for periodic logging */
    static uint32_t s_can_tx_call_count = 0;
    static uint32_t s_last_can_tx_log = 0;
    static uint32_t s_emergency_stop_count = 0;

    s_can_tx_call_count++;

    /* [STUB S002] Check emergency stop flag - skip CAN TX if emergency stop is active */
    if (gateway_check_emergency_stop()) {
        /* Emergency stop active - do not send motor commands */
        /* Motors will naturally stop due to lack of commands */
        s_emergency_stop_count++;
        /* Log emergency stop once per second (1000 cycles) */
        if ((s_can_tx_call_count - s_last_can_tx_log) >= 1000) {
            DebugP_log("[Core1] CAN TX: EMERGENCY STOP ACTIVE (count=%u)\r\n", s_emergency_stop_count);
            s_last_can_tx_log = s_can_tx_call_count;
        }
        return;
    }

    /* [FIX B084] Check if we have one-time commands (enable/disable/zero) vs MIT commands
     * Problem: When single motor enable is sent, ALL 23 motors get CAN frames.
     * Motor 0 receives enable command meant for motor X (wrong motor!).
     * Solution: Detect one-time commands and only send CAN for those motors.
     * MIT commands (mode=255) are sent to ALL motors cyclically. */
    bool has_one_time_command = false;
    for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
        if (g_motor_commands_working[i].mode <= 4) {
            has_one_time_command = true;
            break;
        }
    }

    /* [FIX B093] Determine which motors to process based on update mask or full packet
     * Problem: After Fix B091, data scattered to working[motor_id], not sequential
     *          TX loop needs to process updated positions, not sequential indices
     * Solution: Use g_updated_motors_mask to track which positions have new data
     * - Full packet (all 23 motors): Process all motors (mask==0 means all)
     * - Partial packet (1-22 motors): Process only set bits in mask
     */
    uint8_t loop_limit = GATEWAY_NUM_MOTORS;  /* Always check all motors */
    bool use_mask = (g_updated_motors_mask != 0);  /* Use mask if set */

    /* Group motors by CAN bus for batch transmission */
    can_frame_t* can_frames_ptr[NUM_CAN_BUSES];
    uint16_t frame_count[NUM_CAN_BUSES] = {0};

    /* [BUG FIX B003] Uses global g_frame_buffers instead of local stack allocation
     * Previous: static can_frame_t frame_buffers[NUM_CAN_BUSES][GATEWAY_NUM_MOTORS];
     * This was on stack (2,944 bytes) in 1000Hz loop, risking overflow on bare-metal core
     * Now uses g_frame_buffers allocated at file scope (global/static memory) */
    /* Build CAN frames for each motor */
    for (uint8_t i = 0; i < loop_limit; i++) {
        /* [FIX B093] Skip motors not updated (when using mask) */
        if (use_mask && ((g_updated_motors_mask & (1U << i)) == 0)) {
            continue;  /* This motor not updated, skip it */
        }

        motor_cmd_ipc_t *cmd = &g_motor_commands_working[i];  /* [FIX] Non-const to allow clearing */

        /* [FIX B084] Skip MIT motors when we have one-time commands
         * If has_one_time_command=true, only send motors with mode <= 4
         * Otherwise (all MIT), send all motors */
        if (has_one_time_command && cmd->mode == MOTOR_MODE_MIT_CONTROL) {
            continue;  /* Skip this motor, only send one-time command motors */
        }

        /* [FIX B088] Always use cmd->motor_id for config lookup, not loop index
         * Problem: When single motor enable (index 21) at loop index 0:
         *   - config = motor_get_config(0) → CAN ID 31 (wrong!)
         *   - cmd->motor_id = 21 → CAN ID 44 (correct!)
         *   - Code only fixed config for mode<=4, not MIT mode
         * Solution: Always lookup config using cmd->motor_id directly */
        const motor_config_t *config = NULL;
        uint8_t motor_index = cmd->motor_id;

        /* [FIX B088] Handle both cases: motor_id as index or direct lookup needed
         * For MOTOR_SET: motor_id IS the array index (0-22)
         * For MOTOR_CMD: motor_id is also the array index (0-22)
         * So we can directly use motor_id to lookup config */
        if (motor_index < GATEWAY_NUM_MOTORS) {
            config = motor_get_config(motor_index);
        }

        if (config != NULL && config->can_bus < NUM_CAN_BUSES) {
            /* [FIX B035] Handle one-time commands (enable/disable/zero) vs cyclic commands (motion)
             * Problem: MOTOR_SET commands were being sent cyclically at 1000Hz
             * Solution: Check cmd->mode field to determine command type
             * - mode 0-4: One-time command (disable/enable/zero) → send once, then clear
             * - mode 255 (or other): MIT motion control → send cyclically (default behavior)
             * Reference: draft/ccu_ti/ccu_xgw_gateway.c:1764-1793 */
            uint8_t comm_type;
            bool is_one_time_command = false;

            if (cmd->mode == MOTOR_MODE_ENABLE) {  /* 1 */
                comm_type = COMM_TYPE_MOTOR_ENABLE;
                is_one_time_command = true;
            } else if (cmd->mode == MOTOR_MODE_DISABLE) {  /* 0 */
                comm_type = COMM_TYPE_MOTOR_STOP;
                is_one_time_command = true;
            } else if (cmd->mode == MOTOR_MODE_MECH_ZERO ||
                       cmd->mode == MOTOR_MODE_ZERO_STA ||
                       cmd->mode == MOTOR_MODE_ZERO_STA_MECH) {  /* 2, 3, 4 */
                comm_type = COMM_TYPE_SET_POS_ZERO;
                is_one_time_command = true;
            } else {
                /* MIT Motion control command (cyclic) - mode=255 or not set */
                comm_type = COMM_TYPE_MOTION_CONTROL;
                is_one_time_command = false;
            }

            /* [DEBUG B095] Log enable frames for motors 16, 17 to debug */
            if (is_one_time_command && (motor_index == 16 || motor_index == 17)) {
                DebugP_log("[Core1] Enable: motor_index=%u, CAN_ID=%u, Bus=%u, comm_type=%u\r\n",
                           motor_index, config->motor_id, config->can_bus, comm_type);
            }

            can_frame_t *frame = &g_frame_buffers[config->can_bus][frame_count[config->can_bus]++];

            frame->flags = 0x01;  /* Extended ID */

            if (is_one_time_command) {
                /* One-time command: enable/disable/zero */
                frame->dlc = 8;
                frame->can_id = ((uint32_t)comm_type << 24) | config->motor_id;
                memset(frame->data, 0, sizeof(frame->data));

                /* For mech zero modes, set data[0] = 1 */
                if (cmd->mode == 2 || cmd->mode == 4) {  /* MECH_ZERO or ZERO_STA_MECH */
                    frame->data[0] = 1;
                }

                /* [FIX B037] One-time commands (enable/disable/zero) are sent once, then mode reset to MIT
                 * IMPORTANT: Do NOT reset working buffer during loop! This breaks other motors' CAN frames.
                 * Reset happens AFTER all frames are built and transmitted (see line 535-557).
                 * Reference: draft/ccu_ti/ccu_xgw_gateway.c:1764-1793 */
                /* Mark this motor for reset after transmission (disable needs special handling) */
                /* Actual reset deferred until after CAN TX to avoid race condition */
            } else {
                /* Motion control command (cyclic) */
                /* [FIX B027] Build CAN ID (Robstride protocol) - scale torque correctly */
                /* [FIX B038] motor_cmd_ipc_t now uses float directly (no more ÷100) */
                /* Reference: draft/ccu_ti/ccu_xgw_gateway.c:2108-2113 */
                uint16_t torque_scaled = float_to_uint(cmd->torque * config->direction,
                                                       config->limits.t_min, config->limits.t_max, 16);
                frame->can_id = (COMM_TYPE_MOTION_CONTROL << 24) |
                               ((uint32_t)torque_scaled << 8) |
                               config->motor_id;
                frame->dlc = 8;

                /* [FIX B027] Build CAN data payload - SCALE values correctly using float_to_uint
                 * [FIX B038] motor_cmd_ipc_t now uses float directly (matches reference)
                 * Reference: draft/ccu_ti/ccu_xgw_gateway.c:2117-2123
                 * Data format: [Pos_H, Pos_L, Vel_H, Vel_L, Kp_H, Kp_L, Kd_H, Kd_L] */
                uint16_t pos_scaled = float_to_uint(cmd->position * config->direction,
                                                    config->limits.p_min, config->limits.p_max, 16);
                uint16_t vel_scaled = float_to_uint(cmd->velocity * config->direction,
                                                    config->limits.v_min, config->limits.v_max, 16);
                uint16_t kp_scaled = float_to_uint(cmd->kp, config->limits.kp_min, config->limits.kp_max, 16);
                uint16_t kd_scaled = float_to_uint(cmd->kd, config->limits.kd_min, config->limits.kd_max, 16);

                /* Byte order: MSB first (Big Endian for each 16-bit value) */
                frame->data[0] = (pos_scaled >> 8) & 0xFF;
                frame->data[1] = pos_scaled & 0xFF;
                frame->data[2] = (vel_scaled >> 8) & 0xFF;
                frame->data[3] = vel_scaled & 0xFF;
                frame->data[4] = (kp_scaled >> 8) & 0xFF;
                frame->data[5] = kp_scaled & 0xFF;
                frame->data[6] = (kd_scaled >> 8) & 0xFF;
                frame->data[7] = kd_scaled & 0xFF;
            }

            can_frames_ptr[config->can_bus] = g_frame_buffers[config->can_bus];
        }
    }

    /* Transmit frames on all CAN buses in parallel */
    uint32_t total_frames = 0;
    uint32_t total_sent = 0;

    for (uint8_t bus = 0; bus < NUM_CAN_BUSES; bus++) {
        if (frame_count[bus] > 0) {
            total_frames += frame_count[bus];
            int32_t sent = CAN_TransmitBatch(bus, can_frames_ptr[bus], frame_count[bus]);

            if (sent > 0) {
                total_sent += sent;
                gateway_update_stat(1, 0);  /* Update CAN TX counter */
            }
        }
    }

    /* [FIX B090/B093] Post-transmission cleanup: Reset motors that received one-time commands
     * Problem: Reset loop needs to only check updated motors, not all 23
     * Solution: Use g_updated_motors_mask to track which motors were updated
     * Reference: draft/ccu_ti/ccu_xgw_gateway.c:1764-1793 */
    if (use_mask) {
        /* Only reset motors that were updated this cycle */
        for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
            if ((g_updated_motors_mask & (1U << i)) == 0) {
                continue;  /* Skip motors not updated */
            }
            motor_cmd_ipc_t *cmd = &g_motor_commands_working[i];

            /* Check if this motor sent a one-time command (enable/disable/zero)
             * Modes 0-4 are one-time commands, mode 255 is MIT cyclic */
            if (cmd->mode <= 4) {
                if (cmd->mode == MOTOR_MODE_DISABLE) {
                    /* Disable: Reset ALL motors to safe idle state
                     * Reference: draft/ccu_ti/ccu_xgw_gateway.c:677-695 */
                    for (uint8_t j = 0; j < GATEWAY_NUM_MOTORS; j++) {
                        g_motor_commands_working[j].mode = MOTOR_MODE_MIT_CONTROL;
                        g_motor_commands_working[j].position = 0;
                        g_motor_commands_working[j].velocity = 0;
                        g_motor_commands_working[j].torque = 0;
                        g_motor_commands_working[j].kp = 0;
                        g_motor_commands_working[j].kd = 2.0f;
                    }
                    /* Also reset buffer to prevent stale commands */
                    for (uint8_t j = 0; j < GATEWAY_NUM_MOTORS; j++) {
                        g_motor_commands_buffer[j].mode = MOTOR_MODE_MIT_CONTROL;
                        g_motor_commands_buffer[j].position = 0;
                        g_motor_commands_buffer[j].velocity = 0;
                        g_motor_commands_buffer[j].torque = 0;
                        g_motor_commands_buffer[j].kp = 0;
                        g_motor_commands_buffer[j].kd = 2.0f;
                    }
                    /* Only need to reset once - break after first disable found */
                    break;
                } else {
                    /* Enable/Zero: Reset only this motor to MIT mode
                     * After enable/zero, motor should receive MIT motion control */
                    cmd->mode = MOTOR_MODE_MIT_CONTROL;
                    cmd->position = 0;
                    cmd->velocity = 0;
                    cmd->torque = 0;
                    cmd->kp = 0;
                    cmd->kd = 2.0f;
                }
            }
        }
    } else {
        /* All motors mode - check all motors (e.g., after full MIT packet) */
        for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
            motor_cmd_ipc_t *cmd = &g_motor_commands_working[i];

            /* Check if this motor sent a one-time command (enable/disable/zero)
             * Modes 0-4 are one-time commands, mode 255 is MIT cyclic */
            if (cmd->mode <= 4) {
                if (cmd->mode == MOTOR_MODE_DISABLE) {
                    /* Disable: Reset ALL motors to safe idle state
                     * Reference: draft/ccu_ti/ccu_xgw_gateway.c:677-695 */
                    for (uint8_t j = 0; j < GATEWAY_NUM_MOTORS; j++) {
                        g_motor_commands_working[j].mode = MOTOR_MODE_MIT_CONTROL;
                        g_motor_commands_working[j].position = 0;
                        g_motor_commands_working[j].velocity = 0;
                        g_motor_commands_working[j].torque = 0;
                        g_motor_commands_working[j].kp = 0;
                        g_motor_commands_working[j].kd = 2.0f;
                    }
                    /* Also reset buffer to prevent stale commands */
                    for (uint8_t j = 0; j < GATEWAY_NUM_MOTORS; j++) {
                        g_motor_commands_buffer[j].mode = MOTOR_MODE_MIT_CONTROL;
                        g_motor_commands_buffer[j].position = 0;
                        g_motor_commands_buffer[j].velocity = 0;
                        g_motor_commands_buffer[j].torque = 0;
                        g_motor_commands_buffer[j].kp = 0;
                        g_motor_commands_buffer[j].kd = 2.0f;
                    }
                    /* Only need to reset once - break after first disable found */
                    break;
                } else {
                    /* Enable/Zero: Reset only this motor to MIT mode
                     * After enable/zero, motor should receive MIT motion control */
                    cmd->mode = MOTOR_MODE_MIT_CONTROL;
                    cmd->position = 0;
                    cmd->velocity = 0;
                    cmd->torque = 0;
                    cmd->kp = 0;
                    cmd->kd = 2.0f;
                }
            }
        }
    }

    /* [FIX B093] Clear update mask after processing */
    g_updated_motors_mask = 0;

    /* [DEBUG] Periodic CAN TX logging (once per second) */
    if ((s_can_tx_call_count - s_last_can_tx_log) >= 1000) {
        DebugP_log("[Core1] CAN TX: prepared=%u frames, sent=%u\r\n", total_frames, total_sent);
        s_last_can_tx_log = s_can_tx_call_count;
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

    /* [FIX B027] Initialize motor commands with default MIT values (idle state)
     * Reference: draft/ccu_ti/ccu_xgw_gateway.c:1301-1313
     * This ensures CAN frames are sent with proper default values, not all zeros
     * Default: p=0, v=0, kp=0, kd=2.0, t=0 (safe idle values)
     * IMPORTANT: Must be AFTER motor_mapping_init_core1() to get valid motor configs */
    DebugP_log("[Core1] Initializing motor commands with default MIT values...\r\n");
    for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
        /* Set default MIT command values */
        g_motor_commands_working[i].motor_id = i;  /* motor_id = array index */
        g_motor_commands_working[i].mode = MOTOR_MODE_MIT_CONTROL;  /* 255 = MIT cyclic */
        g_motor_commands_working[i].position = 0;
        g_motor_commands_working[i].velocity = 0;
        g_motor_commands_working[i].kp = 0;
        g_motor_commands_working[i].kd = 2.0f;  /* Default kd=2.0 for stability */
        g_motor_commands_working[i].torque = 0;
    }
    /* Also init buffer with same defaults */
    memcpy(g_motor_commands_buffer, g_motor_commands_working, sizeof(g_motor_commands_buffer));

    /* [FIX B094] Set all motors as updated so default frames are sent immediately
     * Problem: Without this, g_updated_motors_mask=0 → no frames sent until UDP received!
     * Solution: Set mask to all 1s so TX loop sends all default frames on first cycle
     * After first TX, mask will be cleared and only updated motors will be sent */
    g_updated_motors_mask = 0xFFFFFFFF;  /* All 23 motors marked as updated */
    DebugP_log("[Core1] Motor commands initialized: mode=MIT(255), p=0, v=0, kp=0, kd=2.0, t=0 (idle state)\r\n");

    /* Initialize CAN buses */
    CAN_Init();
    DebugP_log("[Core1] CAN buses initialized\r\n");

    /* Register CAN RX callback */
    CAN_RegisterRxCallback(process_can_rx);
    DebugP_log("[Core1] CAN RX callback registered\r\n");

    /* Register IPC callback - BOTH cores must use the SAME client ID */
    DebugP_log("[Core1] Registering IPC callback with client ID=%u\r\n", GATEWAY_IPC_CLIENT_ID);

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

    /* [DEBUG] Log marker to confirm we reach IMU init section */
    DebugP_log("[Core1] *** TIMER DONE, ABOUT TO START CAN RX ***\r\n");

    /* Note: dispatcher_timer removed - not properly integrated (callback never registered) */

    /* Start CAN RX interrupts */
    DebugP_log("[Core1] >>>>> BEFORE CAN_StartRxInterrupts() <<<<<\r\n");
    CAN_StartRxInterrupts();
    DebugP_log("[Core1] >>>>> AFTER CAN_StartRxInterrupts() <<<<<\r\n");

    /* [DEBUG-TRACE] Print marker before IMU init */
    DebugP_log("[Core1] ====== BEFORE IMU INIT (HwiP_construct FIX B020) ======\r\n");

    /* [FIX B020] Re-enabled IMU init with HwiP_construct (matches working reference ccu_ti) */
    /* Initialize IMU (YIS320) */
    DebugP_log("[Core1] About to initialize IMU...\r\n");
    status = imu_uart_isr_init();
    if (status != 0) {
        DebugP_log("[Core1] WARNING: IMU UART ISR init failed! status=%d\r\n", status);
    } else {
        DebugP_log("[Core1] IMU initialized SUCCESSFULLY!\r\n");
    }

    DebugP_log("[Core1] ====== AFTER IMU INIT ======\r\n");

    status = imu_protocol_manager_init(IMU_TYPE_YIS320);
    if (status != 0) {
        DebugP_log("[Core1] WARNING: IMU protocol handler init failed! status=%d\r\n", status);
    } else {
        DebugP_log("[Core1] IMU protocol handler initialized OK\r\n");
    }

    DebugP_log("[Core1] ====== IMU PROTOCOL DONE ======\r\n");

    /* [DEBUG] Add marker to confirm init complete */
    DebugP_log("[Core1] *** INIT COMPLETE, ENTERING MAIN LOOP ***\r\n");

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
        /* [BUG B097 FIX] Drain ring buffer with IPC notify hint
         * g_commands_ready is set by IPC ISR when Core0 notifies
         * This hint helps handle race condition where Core1 checks before Core0's write is visible
         * Main loop runs at 1000Hz, giving natural retry mechanism for cache sync delays */
        bool drain_needed = g_commands_ready;

        /* [DEBUG B097] Trace IPC notify for debugging single motor issue
         * Only log first 10 times to avoid spam */
        static volatile uint32_t ipc_notify_trace_count = 0;
        if (drain_needed && ipc_notify_trace_count < 10) {
            DebugP_log("[Core1] IPC notify received, cycle=%u\r\n", g_cycle_count);
            ipc_notify_trace_count++;
        }

        /* Drain ring buffer if: IPC notify received OR data available */
        uint32_t packets_drained = 0;
        const uint32_t MAX_DRAIN_PER_CYCLE = 10;

        while (packets_drained < MAX_DRAIN_PER_CYCLE &&
               (drain_needed || check_motor_commands_available())) {
            uint8_t count = receive_motor_commands();
            if (count > 0) {
                copy_motor_commands_to_working(count);
                packets_drained++;
                /* Once we successfully drain, clear the hint */
                drain_needed = false;
            } else {
                /* No data - if we have drain_needed hint, it might be cache sync delay
                 * Don't break immediately, try once more before giving up this cycle */
                if (!drain_needed) {
                    break;  /* No hint and no data - exit */
                }
                /* If drain_needed=true, try once more then exit
                 * Main loop will retry next cycle (1ms later) if flag still set */
                drain_needed = false;  /* Prevent infinite retry in same cycle */
            }
        }

        /* Clear IPC notify flag after drain attempt
         * If we drained data, clear flag
         * If we didn't drain (race condition), keep flag set for next cycle retry
         * But clear it after a timeout to prevent stale flag from hanging forever */
        if (packets_drained > 0) {
            g_commands_ready = false;  /* Successfully drained */
            if (ipc_notify_trace_count > 0) {
                DebugP_log("[Core1] Drained %u packets, cycle=%u\r\n", packets_drained, g_cycle_count);
            }
        }
        /* If packets_drained==0, g_commands_ready stays TRUE for retry
         * Flag will eventually be cleared when data arrives or after timeout */

        /* Optional: Log when draining multiple packets (for debugging, disabled by default)
        if (packets_drained > 1 && (g_cycle_count % 1000) == 0) {
            DebugP_log("[Core1] Drained %u packets in one cycle\r\n", packets_drained);
        }
        */

        /* [FIX B096] Removed g_buffer_ready check - copy happens immediately after receive */

        /* 2. Transmit CAN frames to all motors */
        transmit_can_frames();

        /* [FIX B053] Write ALL motor states to shared memory after CAN TX
         * Motors respond asynchronously via RX ISR, which updates local g_motor_states[]
         * After all motors have responded, write all 23 states to shared memory for Core0
         *
         * Strategy: Write states every cycle (1000Hz) after CAN TX
         * This ensures Core0 always gets the latest complete set of motor states */
        gateway_write_motor_states(g_motor_states, GATEWAY_NUM_MOTORS);
        gateway_notify_states_ready();

        /* 3. Periodic heartbeat log every 1000 cycles (1 second) */
        if ((g_cycle_count - last_heartbeat_log) >= 1000) {
            DebugP_log("[Core1] Heartbeat: cycle=%u, timer_isr=%u, ipc_events=%u, ringbuf_init=%d\r\n",
                       g_cycle_count, g_timer_isr_count, g_ipc_event_count, g_ringbuf_initialized);
            /* [DEBUG] CAN TX/RX status */
            DebugP_log("[Core1] CAN: tx_count=%u, rx_count=%u\r\n",
                       dbg_can_tx_count, dbg_can_rx_count);
            /* [DEBUG B046] Track ISR calls per bus */
            uint32_t total_isr_calls = 0;
            for (uint8_t i = 0; i < 8; i++) {
                can_bus_stats_t stats;
                CAN_GetStats(i, &stats);
                total_isr_calls += stats.isr_call_count;
            }
            DebugP_log("[Core1] CAN ISR: total_calls=%u\r\n", total_isr_calls);

            /* [DEBUG B048] Print first interrupt info captured by ISR */
            extern volatile struct {
                uint32_t intr_status;
                uint32_t rx_count_at_intr;
                uint32_t isr_call_count_at_intr;
                uint8_t  bus_id;
                uint8_t  captured;
            } g_first_intr_info[8];

            for (uint8_t i = 0; i < 8; i++) {
                if (g_first_intr_info[i].captured) {
                    DebugP_log("[Core1] CAN%d FirstISR: intr=0x%08X, rx_cnt=%u, isr_cnt=%u\r\n",
                               i,
                               g_first_intr_info[i].intr_status,
                               g_first_intr_info[i].rx_count_at_intr,
                               g_first_intr_info[i].isr_call_count_at_intr);
                }
            }

            /* [DEBUG B060] Log which motors have valid data
             * [FIX B066] Check can_bus != 0xFF (uninitialized value) since motor_id is now array index */
            static bool motor_states_logged = false;
            if (!motor_states_logged) {
                DebugP_log("[Core1] MOTOR_STATES (motors with CAN data):\r\n");
                uint8_t valid_count = 0;
                for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
                    if (g_motor_states[i].can_bus < 8) {  /* Valid CAN bus (0-7) */
                        DebugP_log("  [%2u] idx=%u, pos=%.3f, vel=%.3f\r\n",
                                   i, g_motor_states[i].motor_id,
                                   g_motor_states[i].position,
                                   g_motor_states[i].velocity);
                        valid_count++;
                    }
                }
                DebugP_log("[Core1] Total valid motors: %u/%u\r\n", valid_count, GATEWAY_NUM_MOTORS);
                motor_states_logged = true;
            }

            /* [DEBUG B049] Print RX ISR execution flow counters */
            extern volatile uint32_t dbg_can_isr_rx_entry_count;
            extern volatile uint32_t dbg_can_isr_fifo_read_count;
            extern volatile uint32_t dbg_can_isr_callback_count;
            DebugP_log("[Core1] CAN ISR Flow: rx_entry=%u, fifo_read=%u, callback=%u\r\n",
                       dbg_can_isr_rx_entry_count,
                       dbg_can_isr_fifo_read_count,
                       dbg_can_isr_callback_count);
            /* [DEBUG] IMU UART ISR status */
            DebugP_log("[Core1] IMU ISR: isr_cnt=%u, bytes=%u, frames=%u\r\n",
                       dbg_imu_uart_isr_count, dbg_imu_rx_byte_count, dbg_imu_frame_count);

            /* [IMU DATA] Log parsed IMU data from protocol handler */
            imu_state_t imu_state;
            if (imu_protocol_get_state(&imu_state)) {
                DebugP_log("[Core1] IMU Data: gyro=[%.3f,%.3f,%.3f] rad/s, rpy=[%.3f,%.3f,%.3f] rad\r\n",
                           imu_state.gyroscope[0], imu_state.gyroscope[1], imu_state.gyroscope[2],
                           imu_state.rpy[0], imu_state.rpy[1], imu_state.rpy[2]);
                DebugP_log("[Core1] IMU Quat: [%.4f,%.4f,%.4f,%.4f], ts=%u ms\r\n",
                           imu_state.quaternion[0], imu_state.quaternion[1], imu_state.quaternion[2], imu_state.quaternion[3],
                           (unsigned int)imu_state.timestamp);
            } else {
                DebugP_log("[Core1] IMU: No new data\r\n");
            }

            last_heartbeat_log = g_cycle_count;
        }

        /* [IMU] Read and process UART data from ISR buffer */
        if (imu_uart_is_initialized()) {
            uint8_t imu_rx_buffer[64];
            uint32_t bytes_read;

            bytes_read = imu_uart_read(imu_rx_buffer, sizeof(imu_rx_buffer));
            if (bytes_read > 0) {
                /* [DEBUG] Log when we see YIS320 header (0x59 0x53) - limited logging */
                if (bytes_read >= 2 && imu_rx_buffer[0] == 0x59 && imu_rx_buffer[1] == 0x53) {
                    static uint32_t dbg_header_count = 0;
                    if (dbg_header_count < 5) {  /* Only log first 5 headers */
                        DebugP_log("[Core1] IMU RX: found YIS320 header! bytes=%u\r\n", bytes_read);
                        /* Dump first 20 bytes for inspection */
                        DebugP_log("[Core1] IMU dump: ");
                        for (uint32_t i = 0; i < (bytes_read < 20 ? bytes_read : 20); i++) {
                            DebugP_log("%02X ", imu_rx_buffer[i]);
                        }
                        DebugP_log("\r\n");
                        dbg_header_count++;
                    }
                }
                /* Pass to protocol handler for parsing */
                imu_protocol_process_uart_rx(imu_rx_buffer, (uint16_t)bytes_read);
            }
        } else {
            /* [DEBUG] Log if UART not initialized (should not happen after init) */
            static bool dbg_logged = false;
            if (!dbg_logged) {
                DebugP_log("[Core1] WARNING: IMU UART not initialized!\r\n");
                dbg_logged = true;
            }
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
    /* Initialize SOC and Board */
    System_init();

    Board_init();

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
