/**
 * @file main.c
 * @brief Core 0 (FreeRTOS) - Ethernet Gateway
 *
 * CCU Multicore Project - Core 0:
 * - FreeRTOS
 * - Ethernet (CPSW) + UDP
 * - xGW Protocol handling
 * - IPC Producer/Consumer
 *
 * @author CCU Multicore Project
 * @date 2026-03-18
 */

#include <stdlib.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/ClockP.h>
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"
#include "ti_board_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "../gateway_shared.h"
#include "motor_mapping.h"
#include "log_reader_task.h"
#include "enet/xgw_udp_interface.h"
#include "lwip/tcpip.h"
#include "test_enet_lwip.h"

/* ==========================================================================
 * DEBUG COUNTERS (QA TRACE)
 * ==========================================================================
 * Debug counters placed in shared memory for JTAG visibility.
 * Core0-specific counters defined here.
 */
volatile uint32_t dbg_ipc_recv_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;
volatile uint32_t dbg_udp_rx_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;
volatile uint32_t dbg_udp_tx_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;
volatile uint32_t dbg_error_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;
volatile uint32_t dbg_last_error_code __attribute__((section(".bss.user_shared_mem_debug"))) = 0;
volatile uint32_t dbg_imu_frame_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;

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

#define MAIN_TASK_PRI         (configMAX_PRIORITIES - 1)
#define ENET_LWIP_TASK_PRI    (configMAX_PRIORITIES - 3)  /* Lowered to allow UDP TX to run at 1000Hz */
#define UDP_TX_TASK_PRI       (configMAX_PRIORITIES - 2)  /* Raised - CRITICAL for 1000Hz timing */
#define UDP_RX_TASK_PRI       (configMAX_PRIORITIES - 3)
#define IPC_TASK_PRI          (configMAX_PRIORITIES - 2)

#define MAIN_TASK_SIZE        (4096U/sizeof(configSTACK_DEPTH_TYPE))
#define ENET_LWIP_TASK_SIZE   (16384U/sizeof(configSTACK_DEPTH_TYPE))  /* Increased from 2KB to 16KB to prevent stack overflow */
#define UDP_TX_TASK_SIZE      (2048U/sizeof(configSTACK_DEPTH_TYPE))
#define UDP_RX_TASK_SIZE      (2048U/sizeof(configSTACK_DEPTH_TYPE))
#define IPC_TASK_SIZE         (1024U/sizeof(configSTACK_DEPTH_TYPE))

#define UDP_TX_PERIOD_MS      1   /* 1000 Hz (1ms period) */
#define UDP_RX_PORT           61904  /* Motor commands from PC */
#define UDP_TX_PORT           53489  /* Motor states to PC */

/* [DEBUG] Enable simple UDP TX task with random data for FreeRTOS tick rate testing
 * Set to 1 to test FreeRTOS timing without dependency on Core1/IMU data
 * Set to 0 to use normal UDP TX task with motor states and IMU data
 */
#define DEBUG_SIMPLE_UDP_TX_TASK    0  /* Set to 0 to use optimized normal UDP TX task */

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

typedef struct {
    uint32_t udp_rx_count;
    uint32_t udp_tx_count;
    uint32_t udp_tx_errors;
    uint32_t ipc_rx_count;
    uint32_t parse_errors;
    uint32_t crc_errors;
} core0_stats_t;

/*==============================================================================
 * GLOBAL VARIABLES
 *============================================================================*/

/* Task stacks and handles */
static StackType_t gMainTaskStack[MAIN_TASK_SIZE] __attribute__((aligned(32)));
static StackType_t gEnetLwipTaskStack[ENET_LWIP_TASK_SIZE] __attribute__((aligned(32)));
static StackType_t gUdpTxTaskStack[UDP_TX_TASK_SIZE] __attribute__((aligned(32)));
static StackType_t gIpcTaskStack[IPC_TASK_SIZE] __attribute__((aligned(32)));

static StaticTask_t gMainTaskObj;
static StaticTask_t gEnetLwipTaskObj;
static StaticTask_t gUdpTxTaskObj;
static StaticTask_t gIpcTaskObj;

TaskHandle_t gMainTask = NULL;
TaskHandle_t gUdpTxTask = NULL;
TaskHandle_t gIpcTask = NULL;

/* Statistics */
static core0_stats_t gStats = {0};

/* Motor state buffer from shared memory */
static motor_state_ipc_t g_motor_states[GATEWAY_NUM_MOTORS];

/* Motor command buffer for shared memory */
static motor_cmd_ipc_t g_motor_commands[GATEWAY_NUM_MOTORS];

/* Cached IMU state - resend last known data at 1000Hz even if Core1 updates slower
 * This ensures PC always receives IMU data at configured UDP rate (1000Hz)
 * regardless of YIS320 hardware output rate (typically 100Hz)
 */
static imu_state_ipc_t g_cached_imu_state = {0};
static volatile bool g_imu_has_valid_data = false;

/* IPC callback counter - for statistics
 * BUG FIX B009: Using volatile with critical section for atomic access.
 * On ARM Cortex-R5F, the ++ operator is NOT atomic. Concurrent access from
 * ISR (increment) and task context (read) can cause lost updates.
 * Using FreeRTOS critical sections (taskENTER_CRITICAL/taskEXIT_CRITICAL) for safety.
 */
static volatile uint32_t g_ipc_callback_count = 0;

/*==============================================================================
 * FORWARD DECLARATIONS
 *============================================================================*/

static void freertos_main(void *args);
static void enet_lwip_task_wrapper(void *args);
static void lwip_init_callback(void *arg);
static void udp_tx_task(void *args);
#if DEBUG_SIMPLE_UDP_TX_TASK
static void simple_udp_tx_task(void *args);
#endif
static void ipc_process_task(void *args);
static void ipc_notify_callback_fxn(uint32_t remoteCoreId, uint16_t localClientId,
                                      uint32_t msgValue, int32_t crcStatus, void *args);
static void xgw_udp_rx_callback_wrapper(const uint8_t* data, uint16_t length,
                                         const uint8_t* src_addr, uint16_t src_port);
static int32_t init_ethernet(void);
static int32_t init_udp(void);
static void build_and_send_udp_packet(void);

/*==============================================================================
 * IPC CALLBACK
 *============================================================================*/

/**
 * @brief IPC notification callback
 *
 * Called when Core 1 sends notification
 * Signature matches IpcNotify_FxnCallback
 *
 * IMPORTANT: This runs in ISR context - keep it minimal!
 */
static void ipc_notify_callback_fxn(uint32_t remoteCoreId, uint16_t localClientId,
                                     uint32_t msgValue, int32_t crcStatus, void *args)
{
    (void)crcStatus;
    (void)args;

    /* [QA TRACE T017] Log IPC callback entry */
    DebugP_log("[QA-T017] IPC callback: msg_id=%u\r\n", (unsigned int)msgValue);

    /* [QA TRACE T024] Increment IPC receive counter (Core1→Core0) */
    DEBUG_COUNTER_INC(dbg_ipc_recv_count);

    /* BUG FIX B009: Atomic increment of IPC callback counter.
     * [FIX B019] CRITICAL: This callback runs in ISR context (IpcNotify_isr)
     * Must use taskENTER_CRITICAL_FROM_ISR() instead of taskENTER_CRITICAL()
     *
     * FreeRTOS rule:
     * - taskENTER_CRITICAL() → ONLY from task context
     * - taskENTER_CRITICAL_FROM_ISR() → from ISR context
     *
     * Previous code used taskENTER_CRITICAL() which causes assertion failure
     * because vTaskEnterCritical() cannot be called from ISR context.
     */
    UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
    g_ipc_callback_count++;
    taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);

    /* Call gateway shared memory callback - handles the actual IPC message */
    gateway_core0_ipc_callback(localClientId, (uint16_t)msgValue);

    /* Notify IPC process task */
    if (gIpcTask != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(gIpcTask, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/*==============================================================================
 * UDP TX TASK (1000Hz)
 *============================================================================*/

/**
 * @brief UDP TX task
 *
 * Sends motor states + IMU data to PC at 1000Hz (1ms period)
 * Core1 runs at 1000Hz, UDP TX also at 1000Hz for maximum bandwidth
 * while maintaining real-time performance
 */
static void udp_tx_task(void *args)
{
    (void)args;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(UDP_TX_PERIOD_MS);

    /* [DEBUG] Log timing config to verify tick rate calculation */
    DebugP_log("[Core0] UDP TX: configTICK_RATE_HZ=%d, pdMS_TO_TICKS(1)=%u ticks\r\n",
               configTICK_RATE_HZ, (unsigned int)period);
    DebugP_log("[Core0] UDP TX task started (period=%u ticks, 1000Hz)\r\n", (unsigned int)period);
    DebugP_log("[Core0] UDP TX priority: %u (ENET priority: %u)\r\n",
               UDP_TX_TASK_PRI, ENET_LWIP_TASK_PRI);

    /* [TIMING DEBUG] Measure actual loop rate using hardware timer (more reliable than FreeRTOS tick) */
    uint32_t loop_count = 0;
    uint64_t last_loop_time_us = ClockP_getTimeUsec();

    while (1) {
        /* [PERFORMANCE FIX B023] Remove blocking DebugP_log() calls from 1000Hz loop
         * DebugP_log() uses UART @ 115200 baud which is ~14 bytes/ms - SLOW and BLOCKING!
         * With 9 DebugP_log() calls per loop, serial output becomes the bottleneck.
         * Solution: Only log on startup, use counters for runtime monitoring.
         */

        /* Read motor states from shared memory */
        int32_t count = gateway_read_motor_states(g_motor_states);

        /* [PERFORMANCE] Only send motor states if new data available
         * This reduces UDP TX frequency when Core1 is not updating
         */
        if (count == GATEWAY_NUM_MOTORS) {
            /* All motor states successfully read - build and send UDP packet */
            build_and_send_udp_packet();
            gStats.udp_tx_count++;
        }

        /* [IMU] Always send IMU state at 1000Hz - cache and resend if no new data */
        imu_state_ipc_t imu_state;
        int read_result = gateway_read_imu_state(&imu_state);
        if (read_result == 0) {
            /* New IMU data available - update cache */
            g_cached_imu_state = imu_state;
            g_imu_has_valid_data = true;
        }

        /* Always send IMU data (new or cached) every cycle */
        if (g_imu_has_valid_data) {
            xgw_udp_send_imu_state((xgw_imu_state_t*)&g_cached_imu_state);
        }

        /* Log errors only (no blocking in normal path) */
        if (count < 0) {
            static uint32_t error_count = 0;
            if (++error_count >= 1000) {
                DebugP_log("[Core0] ERROR: gateway_read_motor_states failed\r\n");
                error_count = 0;
            }
        }

        /* [TIMING] Measure actual loop rate every 5 seconds */
        loop_count++;
        uint64_t current_time_us = ClockP_getTimeUsec();
        uint64_t elapsed_us = current_time_us - last_loop_time_us;

        if (elapsed_us >= 5000000ULL) {
            float actual_rate = (float)loop_count / ((float)elapsed_us / 1000000.0f);
            float avg_period_us = (float)elapsed_us / (float)loop_count;
            DebugP_log("[Core0] UDP TX: %.1f Hz, %.2f us (target: 1000 Hz)\r\n",
                       actual_rate, avg_period_us);
            loop_count = 0;
            last_loop_time_us = current_time_us;
        }

        /* Wait for next cycle */
        vTaskDelayUntil(&last_wake_time, period);
    }
}

/**
 * @brief Build and send UDP packet with motor states
 */
static void build_and_send_udp_packet(void)
{
    /* Check if UDP interface is initialized */
    if (!xgw_udp_is_initialized()) {
        /* UDP not ready yet - skip sending */
        return;
    }

    /* Convert IPC format to xGW protocol format */
    xgw_motor_state_t xgw_states[GATEWAY_NUM_MOTORS];

    for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
        xgw_states[i].motor_id = g_motor_states[i].motor_id;
        xgw_states[i].error_code = g_motor_states[i].error_code;
        xgw_states[i].pattern = g_motor_states[i].pattern;
        xgw_states[i].reserved = 0;
        xgw_states[i].position = g_motor_states[i].position / 100.0f;  /* 0.01 rad -> rad */
        xgw_states[i].velocity = g_motor_states[i].velocity / 100.0f;  /* 0.01 rad/s -> rad/s */
        xgw_states[i].torque = g_motor_states[i].torque / 100.0f;      /* 0.01 Nm -> Nm */
        xgw_states[i].temp = g_motor_states[i].temperature / 10.0f;   /* 0.1 °C -> °C */
    }

    /* Send via xGW UDP interface */
    int32_t sent = xgw_udp_send_motor_states(xgw_states, GATEWAY_NUM_MOTORS);

    if (sent > 0) {
        /* Successfully sent */
        gStats.udp_tx_count++;
        /* [QA TRACE T028] Increment UDP TX counter */
        DEBUG_COUNTER_INC(dbg_udp_tx_count);
    } else {
        /* Send failed - increment error counter */
        gStats.udp_tx_errors++;

        /* [QA TRACE T030] Increment error counter */
        DEBUG_COUNTER_INC(dbg_error_count);

        /* Log error every 100th failure to avoid spam */
        static uint32_t error_count = 0;
        if (++error_count >= 100) {
            DebugP_log("[Core0] UDP TX: %d failures\r\n", error_count);
            error_count = 0;
        }
    }
}

#if DEBUG_SIMPLE_UDP_TX_TASK
/**
 * @brief Simple UDP TX task for FreeRTOS tick rate testing
 *
 * Generates random IMU-like data and sends via UDP at 1000Hz.
 * This task isolates FreeRTOS timing from Core1/IMU dependencies.
 *
 * Purpose: Test if configTICK_RATE_HZ=1000 is working correctly.
 * Expected: [Core0] Simple UDP TX: Actual rate=1000.0 Hz
 */
static void simple_udp_tx_task(void *args)
{
    (void)args;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(UDP_TX_PERIOD_MS);

    DebugP_log("[Core0] Simple UDP TX: configTICK_RATE_HZ=%d, pdMS_TO_TICKS(1)=%u ticks\r\n",
               configTICK_RATE_HZ, (unsigned int)period);
    DebugP_log("[Core0] Simple UDP TX task started (period=%u ticks, 1000Hz)\r\n", (unsigned int)period);

    /* Simple random data counter */
    static uint32_t data_counter = 0;
    uint32_t loop_count = 0;
    uint64_t last_loop_time_us = ClockP_getTimeUsec();

    while (1) {
        /* Generate random-like IMU data */
        xgw_imu_state_t imu_data;
        imu_data.imu_id = 0;
        imu_data.reserved = 0;
        imu_data.temp_cdeg = (int16_t)(2500 + (data_counter % 1000));  /* 25-35°C */

        /* Random-like gyro values [rad/s] */
        imu_data.gyro[0] = ((float)((data_counter * 7) % 1000) / 1000.0f) * 0.1f;
        imu_data.gyro[1] = ((float)((data_counter * 11) % 1000) / 1000.0f) * 0.1f;
        imu_data.gyro[2] = ((float)((data_counter * 13) % 1000) / 1000.0f) * 0.1f;

        /* Random-like quaternion [w, x, y, z] */
        imu_data.quat[0] = 1.0f;  /* w */
        imu_data.quat[1] = 0.0f;  /* x */
        imu_data.quat[2] = 0.0f;  /* y */
        imu_data.quat[3] = 0.0f;  /* z */

        /* Random-like euler angles [rad] */
        imu_data.euler[0] = ((float)((data_counter * 17) % 1000) / 1000.0f) * 0.01f;  /* roll */
        imu_data.euler[1] = ((float)((data_counter * 19) % 1000) / 1000.0f) * 0.01f;  /* pitch */
        imu_data.euler[2] = ((float)((data_counter * 23) % 1000) / 1000.0f) * 0.01f;  /* yaw */

        /* Magnetic field (zero for simplicity) */
        imu_data.mag_val[0] = 0.0f;
        imu_data.mag_val[1] = 0.0f;
        imu_data.mag_val[2] = 0.0f;
        imu_data.mag_norm[0] = 0.0f;
        imu_data.mag_norm[1] = 0.0f;
        imu_data.mag_norm[2] = 0.0f;

        data_counter++;

        /* Send via UDP */
        if (xgw_udp_is_initialized()) {
            xgw_udp_send_imu_state(&imu_data);
        }

        /* Measure actual loop rate every 5 seconds */
        loop_count++;
        uint64_t current_time_us = ClockP_getTimeUsec();
        uint64_t elapsed_us = current_time_us - last_loop_time_us;

        if (elapsed_us >= 5000000ULL) {  /* 5 seconds */
            float actual_rate = (float)loop_count / ((float)elapsed_us / 1000000.0f);
            float avg_period_us = (float)elapsed_us / (float)loop_count;
            DebugP_log("[Core0] Simple UDP TX: Actual rate=%.1f Hz, avg_period=%.2f us (target: 1000 Hz, 1000 us)\r\n",
                       actual_rate, avg_period_us);
            DebugP_log("[Core0] Simple UDP TX: loop_count=%u in %llu us\r\n", loop_count,
                       (unsigned long long)elapsed_us);
            loop_count = 0;
            last_loop_time_us = current_time_us;
        }

        /* Wait for next cycle */
        vTaskDelayUntil(&last_wake_time, period);
    }
}
#endif /* DEBUG_SIMPLE_UDP_TX_TASK */

/*==============================================================================
 * UDP RX CALLBACK WRAPPER
 *============================================================================*/

/**
 * @brief UDP RX callback wrapper for xGW UDP interface (UNUSED - see note)
 *
 * NOTE: This callback is NO LONGER REGISTERED to avoid duplicate processing.
 * xgw_udp_interface.c now handles all UDP RX processing directly in
 * xgw_udp_recv_callback() to avoid:
 * 1. Double processing of motor commands
 * 2. Double writes to ring buffer
 * 3. Incorrect udp_rx_count statistics
 *
 * Original architecture: lwIP callback → xgw_udp_interface → wrapper → process
 * Current architecture: lwIP callback → xgw_udp_interface → process (direct)
 *
 * This function is kept for reference only.
 */
static void xgw_udp_rx_callback_wrapper(const uint8_t* data, uint16_t length,
                                         const uint8_t* src_addr, uint16_t src_port)
{
    (void)src_addr;
    (void)src_port;

    /* Process motor command packet */
    if (length >= sizeof(xgw_header_t)) {
        const xgw_header_t* header = (const xgw_header_t*)data;

        switch (header->msg_type) {
            case XGW_MSG_TYPE_MOTOR_CMD:
                xgw_udp_process_motor_cmd(data, length);
                gStats.udp_rx_count++;
                break;

            case XGW_MSG_TYPE_MOTOR_SET:
                xgw_udp_process_motor_set(data, length);
                gStats.udp_rx_count++;
                break;

            default:
                gStats.parse_errors++;
                DebugP_log("[Core0] Unknown xGW message type: 0x%02X\r\n", header->msg_type);
                break;
        }
    }
}

/*==============================================================================
 * IPC PROCESS TASK
 *============================================================================*/

/**
 * @brief IPC process task
 *
 * Handles IPC notifications from Core 1
 * - Motor states are read by UDP TX task via polling (more efficient for high-frequency data)
 * - This task handles low-frequency control messages (emergency stop, heartbeat, etc.)
 */
static void ipc_process_task(void *args)
{
    (void)args;

    DebugP_log("[Core0] IPC process task started\r\n");

    while (1) {
        /* Wait for notification from Core 1 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Motor states are handled by UDP TX task via polling
         * This task handles control messages */

        /* Check for emergency stop condition */
        if (gGatewaySharedMem.emergency_stop_flag != 0) {
            DebugP_log("[Core0] *** EMERGENCY STOP *** - Signal received from Core1\r\n");

            /* Notify application (could trigger GPIO, buzzer, etc.) */
            /* For now, just increment a counter for monitoring */
            gStats.ipc_rx_count++;
        }

        /* Heartbeat is handled automatically via shared memory stats */
        gStats.ipc_rx_count++;
    }
}

/*==============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * @brief Initialize Ethernet
 *
 * NOTE: Ethernet driver and lwIP are initialized in enet_lwip_task_wrapper()
 * which is created after the scheduler starts. This is required because:
 * 1. lwIP needs its tcpip thread to be running first
 * 2. PHY initialization requires the scheduler to be active
 * 3. This function exists only for initialization sequence clarity
 */
static int32_t init_ethernet(void)
{
    DebugP_log("[Core0] Ethernet: deferred to EnetLwip task (lwIP requires scheduler)\r\n");
    return 0;
}

/**
 * @brief Initialize UDP
 */
static int32_t init_udp(void)
{
    int32_t status;

    DebugP_log("[Core0] Initializing xGW UDP interface...\r\n");

    DebugP_log("[DEBUG-udp-01] About to call xgw_udp_init()...\r\n");
    /* Initialize xGW UDP interface */
    status = xgw_udp_init();
    DebugP_log("[DEBUG-udp-02] xgw_udp_init() returned, status=%d\r\n", status);
    if (status != 0) {
        DebugP_log("[Core0] ERROR: xGW UDP interface init failed!\r\n");
        return -1;
    }

    /* UDP RX callback is NOT registered here - xgw_udp_interface.c handles processing directly
     * to avoid duplicate processing. The wrapper below is kept for reference but NOT used. */
    /* status = xgw_udp_register_rx_callback(xgw_udp_rx_callback_wrapper); */

    DebugP_log("[Core0] xGW UDP interface initialized (will start after tcpip_init)\r\n");
    DebugP_log("[DEBUG-udp-03] init_udp() complete\r\n");
    return 0;
}

/**
 * @brief Core 0 initialization
 */
static int32_t core0_init(void)
{
    int32_t status;

    DebugP_log("\r\n");
    DebugP_log("========================================\r\n");
    DebugP_log("  Core 0 (FreeRTOS) Initialization\r\n");
    DebugP_log("========================================\r\n");

    /* Open drivers */
    Drivers_open();
    DebugP_log("[QA-T004] Drivers_open done\r\n");

    status = Board_driversOpen();
    DebugP_assert(status == SystemP_SUCCESS);

    /* Initialize gateway shared memory */
    status = gateway_core0_init();
    if (status != 0) {
        DebugP_log("[Core0] ERROR: Gateway shared memory init failed!\r\n");
        return -1;
    }

#if GATEWAY_USE_LOCKFREE_RINGBUF
    /* Initialize lock-free ring buffers */
    status = gateway_ringbuf_core0_init();
    if (status != 0) {
        DebugP_log("[Core0] ERROR: Lock-free ring buffer init failed!\r\n");
        return -1;
    }
    DebugP_log("[Core0] Lock-free ring buffers initialized\r\n");

    /* Initialize motor mapping (wait for Core 1 to populate config) */
    status = motor_mapping_init_core0();
    if (status != 0) {
        DebugP_log("[Core0] WARNING: Motor mapping init failed!\r\n");
    }
#endif

    /* Initialize Ethernet */
    DebugP_log("[DEBUG-init-01] Calling init_ethernet()...\r\n");
    status = init_ethernet();
    DebugP_log("[DEBUG-init-02] init_ethernet() returned, status=%d\r\n", status);
    if (status != 0) {
        DebugP_log("[Core0] WARNING: Ethernet init failed!\r\n");
    }

    /* Initialize UDP */
    DebugP_log("[DEBUG-init-03] Calling init_udp()...\r\n");
    status = init_udp();
    DebugP_log("[DEBUG-init-04] init_udp() returned, status=%d\r\n", status);
    if (status != 0) {
        DebugP_log("[Core0] WARNING: UDP init failed!\r\n");
    }

    /* Register IPC callback - BOTH cores must use the SAME client ID */
    DebugP_log("[Core0] Registering IPC callback with client ID=%u\r\n", GATEWAY_IPC_CLIENT_ID);
    /* [QA TRACE T014] IPC register entry */
    DebugP_log("[QA-T014] IPC register entry (client=%u)\r\n", GATEWAY_IPC_CLIENT_ID);
    status = IpcNotify_registerClient(GATEWAY_IPC_CLIENT_ID, (IpcNotify_FxnCallback)ipc_notify_callback_fxn, NULL);
    if (status != SystemP_SUCCESS) {
        DebugP_log("[Core0] ERROR: IpcNotify_registerClient failed! status=%d\r\n", status);
    } else {
        DebugP_log("[Core0] IPC callback registered successfully\r\n");
        /* [QA TRACE T014] IPC register done */
        DebugP_log("[QA-T014] IPC register done\r\n");
    }

    /* Wait for Core 1 to be ready */
    DebugP_log("[Core0] Waiting for Core 1 IPC sync...\r\n");
    status = IpcNotify_syncAll(10000);  /* 10 second timeout - MUST match Core 1 timeout */
    if (status != SystemP_SUCCESS) {
        DebugP_log("[Core0] WARNING: IpcNotify_syncAll timeout!\r\n");
    }

    /* Set magic signature AFTER IPC sync is complete to signal Core 1 */
    gateway_core0_finalize();

    DebugP_log("\r\n========================================\r\n");
    DebugP_log("  Core 0 Init Complete!\r\n");
    DebugP_log("========================================\r\n\r\n");

    return 0;
}

/*==============================================================================
 * FREERTOS MAIN
 *============================================================================*/

/**
 * @brief FreeRTOS main task
 *
 * Creates all application tasks and deletes itself
 */
static void freertos_main(void *args)
{
    int32_t status;

    (void)args;

    DebugP_log("[DEBUG-001] freertos_main entry\r\n");

    /* Initialize Core 0 */
    DebugP_log("[DEBUG-002] Calling core0_init()...\r\n");
    status = core0_init();
    DebugP_log("[DEBUG-003] core0_init() returned, status=%d\r\n", status);
    if (status != 0) {
        DebugP_log("[Core0] ERROR: Initialization failed!\r\n");
        vTaskDelete(NULL);
        return;
    }

    /* Create UDP TX task (1000Hz) */
    DebugP_log("[DEBUG-004] Creating UDP TX task...\r\n");
#if DEBUG_SIMPLE_UDP_TX_TASK
    /* [DEBUG] Using simple UDP TX task for FreeRTOS tick rate testing */
    gUdpTxTask = xTaskCreateStatic(
        simple_udp_tx_task,
        "SimpleUdpTx",
        UDP_TX_TASK_SIZE,
        NULL,
        UDP_TX_TASK_PRI,
        gUdpTxTaskStack,
        &gUdpTxTaskObj
    );
    DebugP_log("[DEBUG-004a] Simple UDP TX task created (random data test)\r\n");
#else
    /* Normal UDP TX task with motor states and IMU data */
    gUdpTxTask = xTaskCreateStatic(
        udp_tx_task,
        "UdpTx",
        UDP_TX_TASK_SIZE,
        NULL,
        UDP_TX_TASK_PRI,
        gUdpTxTaskStack,
        &gUdpTxTaskObj
    );
#endif
    DebugP_log("[DEBUG-005] UDP TX task created, ptr=%p\r\n", (void*)gUdpTxTask);
    configASSERT(gUdpTxTask != NULL);

    /* Note: UDP RX is handled by lwIP in EnetLwip task via callback */
    /* xgw_udp_interface registers a callback with lwIP for receiving packets */

    /* Create IPC process task */
    DebugP_log("[DEBUG-006] Creating IPC process task...\r\n");
    gIpcTask = xTaskCreateStatic(
        ipc_process_task,
        "IpcProcess",
        IPC_TASK_SIZE,
        NULL,
        IPC_TASK_PRI,
        gIpcTaskStack,
        &gIpcTaskObj
    );
    DebugP_log("[DEBUG-007] IPC task created, ptr=%p\r\n", (void*)gIpcTask);
    configASSERT(gIpcTask != NULL);

    /* Create Log Reader task - reads Core 1 logs from shared memory */
    DebugP_log("[DEBUG-008] Creating Log Reader task...\r\n");
    status = log_reader_task_create();
    DebugP_log("[DEBUG-009] Log Reader task creation returned, status=%d\r\n", status);
    if (status != 0) {
        DebugP_log("[Core0] WARNING: Log Reader task creation failed!\r\n");
    }

    /* Create Ethernet/LwIP task (runs main_loop in separate task) */
    DebugP_log("[DEBUG-010] Creating Ethernet/LwIP task...\r\n");
    xTaskCreateStatic(
        enet_lwip_task_wrapper,
        "EnetLwip",
        ENET_LWIP_TASK_SIZE,
        NULL,
        ENET_LWIP_TASK_PRI,
        gEnetLwipTaskStack,
        &gEnetLwipTaskObj
    );
    DebugP_log("[DEBUG-011] Ethernet/LwIP task created\r\n");

    /* Note: xGW UDP interface will be started in lwip_init_callback() AFTER tcpip_init() */
    /* This ensures lwIP is properly initialized before creating UDP PCBs */

    DebugP_log("[DEBUG-012] All tasks created successfully\r\n");

    /* Main task no longer needed - delete itself */
    DebugP_log("[DEBUG-013] Deleting main task...\r\n");
    vTaskDelete(NULL);
    DebugP_log("[DEBUG-014] Main task deleted (should not reach here)\r\n");
}

/*==============================================================================
 * LWIP INITIALIZATION (Following ccu_ti pattern)
 *============================================================================*/

/**
 * @brief Callback from tcpip_init() - called when lwIP tcpip thread is ready
 *
 * This is called in the context of the tcpip thread, after lwIP protection
 * (mutexes) has been initialized. Safe to call lwIP APIs here.
 */
static void lwip_init_callback(void *arg)
{
    sys_sem_t *init_sem = (sys_sem_t*)arg;

    DebugP_log("[Core0] lwIP tcpip_init complete - initializing UDP...\r\n");

    /* Start xGW UDP interface (creates UDP PCBs) */
    int32_t status = xgw_udp_start();
    if (status != 0) {
        DebugP_log("[Core0] ERROR: xGW UDP interface start failed!\r\n");
    } else {
        DebugP_log("[Core0] xGW UDP interface started successfully\r\n");
        DebugP_log("[Core0] UDP RX Port: %d (PC -> xGW)\r\n", XGW_UDP_RX_PORT);
        DebugP_log("[Core0] UDP TX Port: %d (xGW -> PC)\r\n", XGW_UDP_TX_PORT);
    }

    /* Signal that initialization is complete */
    if (init_sem != NULL) {
        sys_sem_signal(init_sem);
    }
}

/**
 * @brief Wrapper task for lwIP initialization
 *
 * This task calls enet_lwip_example() which:
 * 1. Initializes ENET driver (CPSW)
 * 2. Initializes PHY and waits for link
 * 3. Calls main_loop() which never returns
 *
 * Follows the pattern from ccu_ti working version.
 */
static void enet_lwip_task_wrapper(void *args)
{
    (void)args;

    DebugP_log("[Core0] Starting ENET + lwIP initialization...\r\n");

    /* This function initializes ENET driver, PHY, and calls main_loop() */
    /* It will never return */
    enet_lwip_example(NULL);

    /* Should never reach here */
    vTaskDelete(NULL);
}

/*==============================================================================
 * ENTRY POINT
 *============================================================================*/

/**
 * @brief Core 0 main entry point
 */
int main(void)
{
    /* [QA TRACE T001] main entry */
    DebugP_log("[QA-T001] main entry\r\n");

    /* Initialize SOC and Board */
    System_init();
    DebugP_log("[QA-T002] System_init done\r\n");

    Board_init();
    DebugP_log("[QA-T003] Board_init done\r\n");

    /* Create main task */
    gMainTask = xTaskCreateStatic(
        freertos_main,
        "freertos_main",
        MAIN_TASK_SIZE,
        NULL,
        MAIN_TASK_PRI,
        gMainTaskStack,
        &gMainTaskObj
    );
    configASSERT(gMainTask != NULL);

    /* Start scheduler */
    /* [QA TRACE T005] Scheduler start */
    DebugP_log("[QA-T005] Scheduler started\r\n");
    vTaskStartScheduler();

    /* Should never reach here */
    DebugP_assertNoLog(0);

    return 0;
}
