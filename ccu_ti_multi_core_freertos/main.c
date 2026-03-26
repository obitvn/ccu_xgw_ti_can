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
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"
#include "ti_board_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "gateway_shared.h"
#include "log_reader_task.h"
#include "enet/xgw_udp_interface.h"
#include "lwip/tcpip.h"

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
#define ENET_LWIP_TASK_PRI    (configMAX_PRIORITIES - 2)
#define UDP_TX_TASK_PRI       (configMAX_PRIORITIES - 3)
#define UDP_RX_TASK_PRI       (configMAX_PRIORITIES - 3)
#define IPC_TASK_PRI          (configMAX_PRIORITIES - 2)

#define MAIN_TASK_SIZE        (8192U/sizeof(configSTACK_DEPTH_TYPE))
#define ENET_LWIP_TASK_SIZE   (4096U/sizeof(configSTACK_DEPTH_TYPE))
#define UDP_TX_TASK_SIZE      (4096U/sizeof(configSTACK_DEPTH_TYPE))
#define UDP_RX_TASK_SIZE      (4096U/sizeof(configSTACK_DEPTH_TYPE))
#define IPC_TASK_SIZE         (2048U/sizeof(configSTACK_DEPTH_TYPE))

#define UDP_TX_PERIOD_MS      2   /* 1000 Hz */
#define UDP_RX_PORT           61904  /* Motor commands from PC */
#define UDP_TX_PORT           53489  /* Motor states to PC */

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

typedef struct {
    uint32_t udp_rx_count;
    uint32_t udp_tx_count;
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
static StackType_t gUdpRxTaskStack[UDP_RX_TASK_SIZE] __attribute__((aligned(32)));
static StackType_t gIpcTaskStack[IPC_TASK_SIZE] __attribute__((aligned(32)));

static StaticTask_t gMainTaskObj;
static StaticTask_t gEnetLwipTaskObj;
static StaticTask_t gUdpTxTaskObj;
static StaticTask_t gUdpRxTaskObj;
static StaticTask_t gIpcTaskObj;

TaskHandle_t gMainTask = NULL;
TaskHandle_t gUdpTxTask = NULL;
TaskHandle_t gUdpRxTask = NULL;
TaskHandle_t gIpcTask = NULL;

/* Statistics */
static core0_stats_t gStats = {0};

/* Motor state buffer from shared memory */
static motor_state_ipc_t g_motor_states[GATEWAY_NUM_MOTORS];

/* Motor command buffer for shared memory */
static motor_cmd_ipc_t g_motor_commands[GATEWAY_NUM_MOTORS];

/* IPC callback counter - for debug in ISR context */
static volatile uint32_t g_ipc_callback_count = 0;

/* Test data received from Core 1 */
static volatile bool g_test_data_from_core1_received = false;
static volatile ringbuf_test_data_t g_test_data_from_core1 = {0};

/*==============================================================================
 * FORWARD DECLARATIONS
 *============================================================================*/

static void freertos_main(void *args);
static void enet_lwip_task_wrapper(void *args);
static void lwip_init_callback(void *arg);
static void udp_tx_task(void *args);
static void udp_rx_task(void *args);
static void ipc_process_task(void *args);
static void ipc_notify_callback_fxn(uint32_t remoteCoreId, uint16_t localClientId,
                                      uint32_t msgValue, int32_t crcStatus, void *args);
static int32_t init_ethernet(void);
static int32_t init_udp(void);
static void process_udp_packet(const uint8_t *data, uint16_t length);
static void build_and_send_udp_packet(void);

/*==============================================================================
 * IPC CALLBACK
 *============================================================================*/

/**
 * @brief IPC notification callback
 *
 * Called when Core 1 sends notification
 * Signature matches IpcNotify_FxnCallback
 */
static void ipc_notify_callback_fxn(uint32_t remoteCoreId, uint16_t localClientId,
                                     uint32_t msgValue, int32_t crcStatus, void *args)
{
    (void)crcStatus;
    (void)args;

    /* Increment callback counter - works in ISR context! */
    g_ipc_callback_count++;

    /* Check for test data notification from Core 1 */
    if (remoteCoreId == CSL_CORE_ID_R5FSS0_1 && msgValue == MSG_CAN_DATA_READY) {
        /* Try to read test data from ring buffer */
        ringbuf_test_data_t test_data;
        int ret = gateway_ringbuf_read_test_core0(&test_data);
        if (ret == GATEWAY_RINGBUF_OK) {
            g_test_data_from_core1 = test_data;
            g_test_data_from_core1_received = true;
        }
    }

    /* Call gateway shared memory callback */
    gateway_core0_ipc_callback(localClientId, (uint16_t)msgValue);

    /* Notify IPC task */
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
 * Sends motor states to PC at 1000Hz
 */
static void udp_tx_task(void *args)
{
    (void)args;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(UDP_TX_PERIOD_MS);

    DebugP_log("[Core0] UDP TX task started (1000Hz)\r\n");

    while (1) {
        /* Read motor states from shared memory */
        int32_t count = gateway_read_motor_states(g_motor_states);

        if (count > 0) {
            /* Build and send UDP packet */
            build_and_send_udp_packet();
            gStats.udp_tx_count++;
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
    } else {
        /* Silent fail - UDP may not be ready yet */
    }
}

/*==============================================================================
 * UDP RX TASK
 *============================================================================*/

/**
 * @brief UDP RX task
 *
 * Receives motor commands from PC
 */
static void udp_rx_task(void *args)
{
    (void)args;

    DebugP_log("[Core0] UDP RX task started\r\n");

    while (1) {
        /* TODO: Wait for UDP packet on port 61904 */
        /* This will be implemented when lwIP is integrated */

        /* Simulate receiving motor commands */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Process UDP packet (motor commands from PC)
 */
static void process_udp_packet(const uint8_t *data, uint16_t length)
{
    /* TODO: Parse xGW protocol header */
    /* TODO: Extract motor commands */
    /* TODO: Validate CRC32 */
    /* TODO: Write to shared memory */
    /* TODO: Notify Core 1 */

    DebugP_log("[Core0] Received UDP packet: %u bytes\r\n", length);
    gStats.udp_rx_count++;
}

/*==============================================================================
 * IPC PROCESS TASK
 *============================================================================*/

/**
 * @brief IPC process task
 *
 * Handles IPC notifications from Core 1 AND sends test IPC to Core 1
 */
static void ipc_process_task(void *args)
{
    (void)args;
    uint32_t notification_count = 0;
    uint32_t last_callback_count = 0;
    uint32_t last_summary_count = 0;
    uint32_t test_tx_count = 0;
    uint32_t test_tx_success = 0;

    DebugP_log("[Core0] IPC process task started\r\n");

    /* Counter for periodic test transmission */
    uint32_t notify_count_for_test = 0;

    while (1) {
        /* Wait for notification */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Process IPC events */
        notification_count++;
        notify_count_for_test++;

        /* Display ISR callback count difference */
        uint32_t callbacks = g_ipc_callback_count - last_callback_count;
        DebugP_log("[Core0] NOTIFY RX from Core1: count=%u, callbacks=%u\r\n",
                   notification_count, callbacks);
        last_callback_count = g_ipc_callback_count;

        /* Log summary every 10 notifications */
        if ((notification_count - last_summary_count) >= 10) {
            DebugP_log("[Core0] *** RX SUMMARY: total=%u notifications received ***\r\n",
                       notification_count);
            last_summary_count = notification_count;
        }

        /* Send test IPC to Core 1 every 10 Core 1 notifications */
        if ((notify_count_for_test % 10) == 0) {
            test_tx_count++;
            int32_t status = IpcNotify_sendMsg(CSL_CORE_ID_R5FSS0_1, GATEWAY_IPC_CLIENT_ID, MSG_ETH_DATA_READY, 1);

            if (status == SystemP_SUCCESS) {
                test_tx_success++;
                DebugP_log("[Core0] TEST: IPC TX to Core1: count=%u, success=%u\r\n",
                           test_tx_count, test_tx_success);
            } else {
                DebugP_log("[Core0] TEST: IPC TX FAILED! status=%d\r\n", status);
            }
        }

        /* Test shared memory + IPC every 20 notifications */
        if ((notify_count_for_test % 20) == 0) {
            /* Create test pattern: 1, 2, 3, 4, 5, 6, 7, 8 */
            ringbuf_test_data_t test_data;
            test_data.timestamp = g_ipc_callback_count;
            for (int i = 0; i < 8; i++) {
                test_data.data[i] = i + 1;
            }

            /* Write test data to ring buffer */
            int write_status = gateway_ringbuf_write_test_core0(&test_data);
            if (write_status == GATEWAY_RINGBUF_OK) {
                DebugP_log("[Core0] RINGBUF: Written test data [1,2,3,4,5,6,7,8] seq=%u\r\n",
                           test_data.sequence);

                /* Notify Core 1 that test data is ready */
                int notify_status = gateway_ringbuf_core0_notify();
                if (notify_status == 0) {
                    DebugP_log("[Core0] RINGBUF: Test data notification sent to Core 1\r\n");
                } else {
                    DebugP_log("[Core0] RINGBUF ERROR: Failed to notify Core 1! notify_status=%d\r\n", notify_status);
                }
            } else if (write_status == GATEWAY_RINGBUF_FULL) {
                DebugP_log("[Core0] RINGBUF: Buffer full - cannot write test data\r\n");
            } else {
                DebugP_log("[Core0] RINGBUF ERROR: Failed to write test data! status=%d\r\n", write_status);
            }
        }

        /* Check for test data received from Core 1 */
        if (g_test_data_from_core1_received) {
            DebugP_log("[Core0] *** TEST DATA from Core 1: seq=%u [%u %u %u %u %u %u %u %u] ***\r\n",
                       g_test_data_from_core1.sequence,
                       g_test_data_from_core1.data[0], g_test_data_from_core1.data[1],
                       g_test_data_from_core1.data[2], g_test_data_from_core1.data[3],
                       g_test_data_from_core1.data[4], g_test_data_from_core1.data[5],
                       g_test_data_from_core1.data[6], g_test_data_from_core1.data[7]);
            g_test_data_from_core1_received = false;  /* Clear flag */
        }

        /* Motor states are read in UDP TX task */
        gStats.ipc_rx_count++;
    }
}

/*==============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * @brief Initialize Ethernet
 */
static int32_t init_ethernet(void)
{
    DebugP_log("[Core0] Initializing Ethernet...\r\n");

    /* TODO: Initialize Ethernet driver */
    /* TODO: Initialize lwIP stack in separate task AFTER scheduler starts */
    /* TODO: Configure IP address */

    DebugP_log("[Core0] Ethernet initialized (lwIP deferred)\r\n");
    return 0;
}

/**
 * @brief Initialize UDP
 */
static int32_t init_udp(void)
{
    int32_t status;

    DebugP_log("[Core0] Initializing xGW UDP interface...\r\n");

    /* Initialize xGW UDP interface */
    status = xgw_udp_init();
    if (status != 0) {
        DebugP_log("[Core0] ERROR: xGW UDP interface init failed!\r\n");
        return -1;
    }

    DebugP_log("[Core0] xGW UDP interface initialized (will start after tcpip_init)\r\n");
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
#endif

    /* Initialize Ethernet */
    status = init_ethernet();
    if (status != 0) {
        DebugP_log("[Core0] WARNING: Ethernet init failed!\r\n");
    }

    /* Initialize UDP */
    status = init_udp();
    if (status != 0) {
        DebugP_log("[Core0] WARNING: UDP init failed!\r\n");
    }

    /* Register IPC callback - BOTH cores must use the SAME client ID */
    DebugP_log("[Core0] Registering IPC callback with client ID=%u\r\n", GATEWAY_IPC_CLIENT_ID);
    status = IpcNotify_registerClient(GATEWAY_IPC_CLIENT_ID, (IpcNotify_FxnCallback)ipc_notify_callback_fxn, NULL);
    if (status != SystemP_SUCCESS) {
        DebugP_log("[Core0] ERROR: IpcNotify_registerClient failed! status=%d\r\n", status);
    } else {
        DebugP_log("[Core0] IPC callback registered successfully\r\n");
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

    /* Initialize Core 0 */
    status = core0_init();
    if (status != 0) {
        DebugP_log("[Core0] ERROR: Initialization failed!\r\n");
        vTaskDelete(NULL);
        return;
    }

    /* Create UDP TX task (1000Hz) */
    gUdpTxTask = xTaskCreateStatic(
        udp_tx_task,
        "UdpTx",
        UDP_TX_TASK_SIZE,
        NULL,
        UDP_TX_TASK_PRI,
        gUdpTxTaskStack,
        &gUdpTxTaskObj
    );
    configASSERT(gUdpTxTask != NULL);

    /* Create UDP RX task */
    gUdpRxTask = xTaskCreateStatic(
        udp_rx_task,
        "UdpRx",
        UDP_RX_TASK_SIZE,
        NULL,
        UDP_RX_TASK_PRI,
        gUdpRxTaskStack,
        &gUdpRxTaskObj
    );
    configASSERT(gUdpRxTask != NULL);

    /* Create IPC process task */
    gIpcTask = xTaskCreateStatic(
        ipc_process_task,
        "IpcProcess",
        IPC_TASK_SIZE,
        NULL,
        IPC_TASK_PRI,
        gIpcTaskStack,
        &gIpcTaskObj
    );
    configASSERT(gIpcTask != NULL);

    /* Create Log Reader task - reads Core 1 logs from shared memory */
    status = log_reader_task_create();
    if (status != 0) {
        DebugP_log("[Core0] WARNING: Log Reader task creation failed!\r\n");
    }

    /* Create Ethernet/LwIP task (runs main_loop in separate task) */
    xTaskCreateStatic(
        enet_lwip_task_wrapper,
        "EnetLwip",
        ENET_LWIP_TASK_SIZE,
        NULL,
        ENET_LWIP_TASK_PRI,
        gEnetLwipTaskStack,
        &gEnetLwipTaskObj
    );

    /* Note: xGW UDP interface will be started in lwip_init_callback() AFTER tcpip_init() */
    /* This ensures lwIP is properly initialized before creating UDP PCBs */

    DebugP_log("[Core0] All tasks created successfully\r\n");

    /* Main task no longer needed - delete itself */
    vTaskDelete(NULL);
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
 * This task calls tcpip_init() which creates the tcpip thread and waits
 * for initialization to complete. Follows the pattern from ccu_ti.
 */
static void enet_lwip_task_wrapper(void *args)
{
    (void)args;
    err_t err;
    sys_sem_t init_sem;

    DebugP_log("[Core0] Starting lwIP initialization...\r\n");

    /* Create semaphore for tcpip_init completion */
    err = sys_sem_new(&init_sem, 0);
    if (err != ERR_OK) {
        DebugP_log("[Core0] ERROR: Failed to create init semaphore!\r\n");
        vTaskDelete(NULL);
        return;
    }

    /* Initialize lwIP stack - this creates the tcpip thread */
    /* lwip_init_callback will be called when tcpip thread is ready */
    tcpip_init(lwip_init_callback, &init_sem);

    /* Wait for tcpip_init to complete */
    sys_sem_wait(&init_sem);
    sys_sem_free(&init_sem);

    DebugP_log("[Core0] lwIP initialization complete\r\n");

    /* TODO: Initialize Ethernet driver (CPSW) */
    /* TODO: Configure IP address */

    /* TODO: Main loop for Ethernet driver polling */
    /* For now, just keep task alive */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

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
    /* Initialize SOC and Board */
    System_init();
    Board_init();

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
    vTaskStartScheduler();

    /* Should never reach here */
    DebugP_assertNoLog(0);

    return 0;
}
