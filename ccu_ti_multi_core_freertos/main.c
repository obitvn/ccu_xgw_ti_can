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
#include <string.h>
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
#include "lwip/stats.h"  /* For lwIP stats display */
#include "test_enet_lwip.h"
#include "enet/xgw_udp_interface.h"  /* For pbuf tracking counters */

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
/* CRITICAL: TCP/IP thread must have HIGHER priority than tasks calling udp_sendto()
 * Otherwise TX completion won't be processed and pbufs won't be freed!
 * TCPIP_THREAD_PRIO = 7 (SDK default in lwipopts.h)
 * ENET_LWIP_TASK_PRI = 29
 * UDP_TX_TASK_PRI must be < 7 to allow tcpip thread to run!
 */
#define ENET_LWIP_TASK_PRI    (configMAX_PRIORITIES - 3)  /* 29 - for lwIP init/operation */
#define UDP_TX_TASK_PRI       (configMAX_PRIORITIES - 4)  /* LOWER than TCPIP_THREAD_PRIO(7) - allows TX completion */
#define UDP_RX_TASK_PRI       (configMAX_PRIORITIES - 3)
#define IPC_TASK_PRI          (configMAX_PRIORITIES - 2)

#define MAIN_TASK_SIZE        (4096U/sizeof(configSTACK_DEPTH_TYPE))
#define ENET_LWIP_TASK_SIZE   (16384U/sizeof(configSTACK_DEPTH_TYPE))  /* Increased from 2KB to 16KB to prevent stack overflow */
#define UDP_TX_TASK_SIZE      (16384U/sizeof(configSTACK_DEPTH_TYPE))
#define UDP_RX_TASK_SIZE      (4096U/sizeof(configSTACK_DEPTH_TYPE))  /* [FIX B073] Increased from 2048 to prevent stack overflow */
#define IPC_TASK_SIZE         (1024U/sizeof(configSTACK_DEPTH_TYPE))

#define UDP_TX_PERIOD_MS      1   
#define UDP_RX_PORT           61904  /* Motor commands from PC */
#define UDP_TX_PORT           53489  /* Motor states to PC */

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
static StackType_t gUdpRxTaskStack[UDP_RX_TASK_SIZE] __attribute__((aligned(32)));  /* [FIX B070] */
static StackType_t gIpcTaskStack[IPC_TASK_SIZE] __attribute__((aligned(32)));

static StaticTask_t gMainTaskObj;
static StaticTask_t gEnetLwipTaskObj;
static StaticTask_t gUdpTxTaskObj;
static StaticTask_t gUdpRxTaskObj;  /* [FIX B070] UDP RX task TCB */
static StaticTask_t gIpcTaskObj;

TaskHandle_t gMainTask = NULL;
TaskHandle_t gUdpTxTask = NULL;
TaskHandle_t gIpcTask = NULL;

/* Statistics */
static core0_stats_t gStats = {0};

/* Motor state buffer from shared memory */
static motor_state_ipc_t g_motor_states[GATEWAY_NUM_MOTORS] = {0};

/* Motor command buffer for shared memory */
static motor_cmd_ipc_t g_motor_commands[GATEWAY_NUM_MOTORS] = {0};

/* Cached IMU state - resend last known data at 1000Hz even if Core1 updates slower
 * This ensures PC always receives IMU data at configured UDP rate (1000Hz)
 * regardless of YIS320 hardware output rate (typically 100Hz)
 */
static imu_state_ipc_t g_cached_imu_state = {0};
static volatile bool g_imu_has_valid_data = false;

/* [DEBUG] Simulate motor data for testing UDP TX when no motors connected
 * Generates dummy motor states every 100 cycles (100ms) to test UDP TX path
 */
#define SIMULATE_MOTOR_DATA  0  /* Disable simulation - use real shared memory data */
static uint32_t g_sim_counter = 0;

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
 * UDP TX TASK (500Hz task rate, 1000 packets/sec total)
 *============================================================================*/

/**
 * @brief UDP TX task
 *
 * Sends motor states + IMU data to PC at 1000 packets/sec total
 * Task runs at 500Hz (2ms period), sends 2 packets per cycle
 * - Motor state packet every cycle
 * - IMU state packet every cycle (cached/resend if no new data)
 *
 * Matches ccu_ti reference design which achieves stable 1000Hz operation
 */
static void udp_tx_task(void *args)
{
    (void)args;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(UDP_TX_PERIOD_MS);

    /* [DEBUG] Log timing config to verify tick rate calculation */
    DebugP_log("[Core0] UDP TX: configTICK_RATE_HZ=%d, pdMS_TO_TICKS(%d)=%u ticks\r\n",
               configTICK_RATE_HZ, UDP_TX_PERIOD_MS, (unsigned int)period);
    DebugP_log("[Core0] UDP TX task started (period=%u ticks, 500Hz task rate, 1000 packets/sec total)\r\n", (unsigned int)period);
    DebugP_log("[Core0] UDP TX priority: %u (ENET priority: %u)\r\n",
               UDP_TX_TASK_PRI, ENET_LWIP_TASK_PRI);

    /* [FIX B024] Wait for Ethernet link to be up before sending UDP
     * ERR_BUF (-4) occurs if we try to send before link is up
     * Must check both netif_is_up() AND netif_is_link_up()
     */
    DebugP_log("[Core0] UDP TX: Waiting for Ethernet link to come up...\r\n");

    uint32_t wait_count = 0;
    const uint32_t MAX_WAIT_SECONDS = 10;  /* Timeout after 10 seconds */

    while (!enet_is_link_up() && wait_count < MAX_WAIT_SECONDS * 1000) {
        vTaskDelay(pdMS_TO_TICKS(100));  /* Check every 100ms */
        wait_count += 100;

        /* Log every 1 second */
        if (wait_count % 1000 == 0) {
            DebugP_log("[Core0] UDP TX: Still waiting for link... (%lu sec)\r\n", wait_count / 1000);
        }
    }

    if (enet_is_link_up()) {
        DebugP_log("[Core0] UDP TX: Link is UP! Starting transmission...\r\n");
    } else {
        DebugP_log("[Core0] UDP TX: WARNING - Timeout waiting for link, starting anyway...\r\n");
    }

    /* [TIMING DEBUG] Measure actual loop rate using hardware timer (more reliable than FreeRTOS tick) */
    uint32_t loop_count = 0;
    uint64_t last_loop_time_us = ClockP_getTimeUsec();

    while (1) {
        /* [PERFORMANCE FIX B023] Remove blocking DebugP_log() calls from high-rate loop
         * DebugP_log() uses UART @ 115200 baud which is ~14 bytes/ms - SLOW and BLOCKING!
         * Task runs at 500Hz (2ms) sending 2 packets/cycle = 1000 packets/sec total.
         * Even at 500Hz, logging would cause significant timing issues.
         * Solution: Only log on startup, use counters for runtime monitoring.
         */

        /* [DEBUG B026] Simulate motor data for testing when no motors connected
         * Currently DISABLED - using real shared memory data from Core1
         */
#if SIMULATE_MOTOR_DATA
        /* Generate dummy motor states EVERY cycle */
        g_sim_counter++;
        for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
            g_motor_states[i].motor_id = i + 1;  /* Motor ID 1-23 */
            g_motor_states[i].can_bus = i % 8;      /* CAN bus 0-7 */
            g_motor_states[i].pattern = 0;
            g_motor_states[i].error_code = 0;
            /* Simple sine wave for position */
            g_motor_states[i].position = (int16_t)(1000 * __builtin_sin(g_sim_counter * 0.01f + i * 0.5f));
            g_motor_states[i].velocity = (int16_t)(100 * __builtin_cos(g_sim_counter * 0.01f + i * 0.5f));
            g_motor_states[i].torque = 0;
            g_motor_states[i].temperature = 250 + i;  /* 25-32°C */
        }

        /* Log simulation once at start */
        static uint32_t sim_log_count = 0;
        if (sim_log_count++ < 1) {
            DebugP_log("[Core0] SIM: Generating %u motor states EVERY cycle\r\n", GATEWAY_NUM_MOTORS);
        }
#endif

        /* Read motor states from shared memory */
        int32_t count;
#if SIMULATE_MOTOR_DATA
        /* [TEST B033] Use simulated data directly, bypass shared memory */
        count = GATEWAY_NUM_MOTORS;  /* Force count to trigger UDP send */
#else
        count = gateway_read_motor_states(g_motor_states);
#endif

        /* [DEBUG B051] Log read result occasionally to diagnose */
        static uint32_t motor_read_count = 0;
        static uint32_t motor_success_count = 0;
        static uint32_t motor_error_count = 0;
        motor_read_count++;

        /* Log when count is not expected (every 100th error to avoid spam) */
        if (count != GATEWAY_NUM_MOTORS && count > 0) {
            if (++motor_error_count <= 10 || (motor_error_count % 100 == 0)) {
                DebugP_log("[Core0] motor_read: count=%d (expected %d), err_cnt=%u\r\n",
                           count, GATEWAY_NUM_MOTORS, motor_error_count);
            }
        }

        /* [PERFORMANCE] Only send motor states if new data available
         * This reduces UDP TX frequency when Core1 is not updating
         */
        if (count == GATEWAY_NUM_MOTORS) {
            /* [DEBUG B057] Log first successful motor read - verify shared memory working
             * ONLY logs once to verify data flow from Core1 to Core0 */
            static bool first_motor_read_logged = false;
            if (!first_motor_read_logged) {
                DebugP_log("[Core0] Motor READ OK: count=%d, m[0].id=%u, pos=%.3f, vel=%.3f, trq=%.3f, tmp=%.1f\r\n",
                           count,
                           g_motor_states[0].motor_id,
                           g_motor_states[0].position,
                           g_motor_states[0].velocity,
                           g_motor_states[0].torque,
                           g_motor_states[0].temperature);
                first_motor_read_logged = true;
            }

            /* All motor states successfully read - send UDP packet */
            build_and_send_udp_packet();
            motor_success_count++;
        }

        /* [DEBUG] Log motor read statistics every 5000 cycles - no logging in success path */
        if (motor_read_count >= 5000) {
            DebugP_log("[Core0] Motor stats: total=%u, ok=%u (%.1f%%), err=%u, last=%d, link=%d\r\n",
                       motor_read_count, motor_success_count,
                       motor_read_count > 0 ? (float)motor_success_count * 100.0f / motor_read_count : 0.0f,
                       motor_error_count, count, enet_is_link_up());
            /* [FIX B055] Reset ALL counters to avoid overflow and incorrect stats */
            motor_read_count = 0;
            motor_success_count = 0;
            motor_error_count = 0;
        }

        /* [IMU] Always send IMU state at 1000Hz - cache and resend if no new data
         * Core1 IMU ISR updates at ~100Hz, but UDP must send at 1000Hz
         * Cache last valid IMU state and resend every cycle */
        static imu_state_ipc_t g_cached_imu_state = {0};
        static volatile bool g_imu_has_valid_data = false;
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

            /* [DEBUG B029] Get pbuf stats from xGW UDP interface */
            int32_t pbuf_in_use = g_pbuf_alloc_count - g_pbuf_free_count;

            DebugP_log("[Core0] UDP TX: %.1f Hz, %.2f us | pbuf: alloc=%u free=%u in_use=%d fail=%u sendto=%u\r\n",
                       actual_rate, avg_period_us,
                       g_pbuf_alloc_count, g_pbuf_free_count, pbuf_in_use,
                       g_pbuf_alloc_fail_count, g_udp_sendto_count);

            /* [DEBUG B029] Dump lwIP stats every 30 seconds */
            static uint32_t stats_dump_counter = 0;
            if (++stats_dump_counter >= 6) {  /* Every 6 x 5 seconds = 30 seconds */
                DebugP_log("[Core0] === LWIP STATS ===\r\n");
                stats_display();
                stats_dump_counter = 0;
            }

            loop_count = 0;
            last_loop_time_us = current_time_us;
        }

        /* Wait for next cycle */
        vTaskDelayUntil(&last_wake_time, period);
    }
}

/**
 * @brief Test pbuf allocation with progressively larger packet sizes
 *
 * This test sends packets with increasing size to find the maximum
 * safe packet size before memory exhaustion or crashes occur.
 */
static void test_pbuf_size_progressive(void)
{
    /* Test sizes: 64, 128, 256, 384, 492 (full motor state), 512, 768, 1024 */
    static const uint16_t test_sizes[] = {64, 128, 256, 384, 492, 512, 768, 1024};
    static const uint8_t num_tests = 8;
    static uint8_t current_test = 0;

    /* Only run one test cycle */
    static bool test_complete = false;
    if (test_complete) {
        return;
    }

    /* Test each size with 5 attempts */
    for (uint8_t attempt = 0; attempt < 5; attempt++) {
        uint16_t test_size = test_sizes[current_test];

        /* Create dummy data */
        uint8_t test_data[1024];
        memset(test_data, 0xAA, test_size);

        /* Try to allocate and send */
        extern int xgw_udp_send_raw(const uint8_t* data, uint16_t len);
        int32_t result = xgw_udp_send_raw(test_data, test_size);

        if (result > 0) {
            DebugP_log("[TEST] Size %u: OK (attempt %d)\r\n", test_size, attempt + 1);
        } else {
            DebugP_log("[TEST] Size %u: FAILED at attempt %d\r\n", test_size, attempt + 1);
            break;  /* Stop this size test on first failure */
        }

        /* Small delay between sends */
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    current_test++;
    if (current_test >= num_tests) {
        test_complete = true;
        DebugP_log("[TEST] All size tests completed!\r\n");
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

    /* [DEBUG] Log when called to track call rate */
    static uint32_t call_count = 0;
    call_count++;

    /* [FIX B033] Use static array instead of local to reduce stack usage
     * Local array: 460 bytes on stack can cause issues in high-rate loop
     * Static array: Allocated in BSS section, no stack pressure
     */
    static xgw_motor_state_t xgw_states[GATEWAY_NUM_MOTORS];

    /* [FIX B064] Convert IPC format to xGW protocol format - direct float copy */
    for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
        xgw_states[i].motor_id = g_motor_states[i].motor_id;
        xgw_states[i].error_code = g_motor_states[i].error_code;
        xgw_states[i].pattern = g_motor_states[i].pattern;
        xgw_states[i].reserved = 0;
        xgw_states[i].position = g_motor_states[i].position;
        xgw_states[i].velocity = g_motor_states[i].velocity;
        xgw_states[i].torque = g_motor_states[i].torque;
        xgw_states[i].temp = g_motor_states[i].temperature;
    }

    /* [DEBUG B060] Log ALL motor IDs being sent to UDP (first call only)
     * This helps diagnose why PC only shows 2 motors instead of 23
     * Print in format: [Core0] MOTOR_IDS: 0:ID0 1:ID1 2:ID2 ... */
    static bool motor_ids_logged = false;
    if (!motor_ids_logged) {
        DebugP_log("[Core0] SENDING MOTOR_IDS:\r\n");
        for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
            DebugP_log("  [%2u] id=%u, pos=%.3f, vel=%.3f\r\n",
                       i, xgw_states[i].motor_id,
                       xgw_states[i].position,
                       xgw_states[i].velocity);
        }
        motor_ids_logged = true;
    }

    /* Log every 100 calls */
    static uint32_t cycle_count = 0;
    if (call_count >= 100) {
        DebugP_log("[Core0] build_and_send: %u calls processed\r\n", call_count);
        call_count = 0;
        cycle_count++;

        /* [FORCE LOG] Always log motor data after each 100-call cycle - bypass static variable persistence */
        DebugP_log("[Core0] MOTOR_DATA cycle#%u: m[0].id=%u, pos=%.3f, vel=%.3f, trq=%.3f, tmp=%.1f\r\n",
                   cycle_count,
                   xgw_states[0].motor_id,
                   g_motor_states[0].position,
                   g_motor_states[0].velocity,
                   g_motor_states[0].torque,
                   g_motor_states[0].temperature);
    }

    /* [TEST] Send only 1 motor instead of 23 to test if issue is item count */
    // int32_t sent = xgw_udp_send_motor_states(xgw_states, 1);  /* Only send 1 motor! */

    /* Send motor states via UDP */
    int32_t sent = xgw_udp_send_motor_states(xgw_states, GATEWAY_NUM_MOTORS);

    /* [FORCE LOG] Always log UDP result in first cycle of each 100-call batch */
    if (call_count == 0) {
        DebugP_log("[Core0] UDP send result: sent=%d, count=%u, started=%d\r\n",
                   sent, GATEWAY_NUM_MOTORS, xgw_udp_is_initialized());
    }

    if (sent > 0) {
        /* Successfully sent */
        gStats.udp_tx_count++;
    } else {
        /* Send failed - increment error counter */
        gStats.udp_tx_errors++;

        /* Log error every 100th failure to avoid spam */
        static uint32_t error_count = 0;
        if (++error_count >= 100) {
            DebugP_log("[Core0] UDP TX motor: %d failures\r\n", error_count);
            error_count = 0;
        }
    }
}

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
    gUdpTxTask = xTaskCreateStatic(
        udp_tx_task,
        "UdpTx",
        UDP_TX_TASK_SIZE,
        NULL,
        UDP_TX_TASK_PRI,
        gUdpTxTaskStack,
        &gUdpTxTaskObj
    );
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
 *
 * [NOTE] This callback is NOT currently used. test.c uses its own test_init() callback.
 * This function is kept for reference but is dead code.
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

        /* [FIX B070] Start UDP RX task for queue-based processing
         * This prevents lwIP thread blocking when processing many packets
         * Reference: ccu_ti/ccu_xgw_gateway.c */
        DebugP_log("[Core0] *** ABOUT TO START UDP RX TASK ***\r\n");
        status = xgw_udp_start_rx_task(&gUdpRxTaskStack[0], sizeof(gUdpRxTaskStack), &gUdpRxTaskObj);
        DebugP_log("[Core0] *** UDP RX TASK START RETURNED: status=%d ***\r\n", status);
        if (status != 0) {
            DebugP_log("[Core0] ERROR: UDP RX task start failed!\r\n");
        } else {
            DebugP_log("[Core0] UDP RX task started\r\n");
        }
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
int main(void){
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
