/**
 * @file ccu_xgw_gateway.c
 * @brief ccu_ti xGW Gateway implementation for Core0 (FreeRTOS)
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.c]
 * [PLACEMENT: CORE0 (FreeRTOS) - UDP/CAN gateway main application]
 *
 * Key changes for multicore:
 * - Removed direct CAN interface access (now handled by Core1)
 * - Removed direct IMU UART access (now handled by Core1)
 * - Added IPC integration via gateway_shared.h
 * - CAN TX task moved to Core1 (bare metal, 1000Hz)
 * - Motor state processing moved to Core1
 * - UDP RX/TX tasks remain on Core0 (lwIP dependency)
 *
 * Data Flow:
 * 1. UDP RX (port 61904) -> UDP RX Task -> Parse -> IPC -> Core1 -> CAN Bus
 * 2. CAN RX -> Core1 -> IPC -> UDP TX Task -> UDP (port 53489)
 * 3. IMU UART -> Core1 -> IPC -> UDP TX Task -> UDP (port 53489)
 *
 * @author Migrated from ccu_ti by Chu Tien Thinh
 * @date 2026-03-27
 */

/*==============================================================================
 * FEATURE CONFIGURATION
 *============================================================================*/

/**
 * @brief Symmetric Kinetic Synchronization (SKS)
 *
 * When ENABLED (1): Motors are sent in symmetric pairs with distal-to-proximal order
 * When DISABLED (0): Motors are sent in sequential order (0-22)
 *
 * Note: SKS transmission ordering is now handled by Core1 CAN TX task.
 * This flag is kept for protocol compatibility but does not affect Core0.
 */
#ifndef ENABLE_SYMMETRIC_KINETIC_SYNC
#define ENABLE_SYMMETRIC_KINETIC_SYNC    0
#endif

/**
 * @brief Automatic CAN Bus-Off Recovery
 *
 * Note: Auto-recovery is now handled by Core1 (bare metal).
 * This flag is kept for protocol compatibility but does not affect Core0.
 */
#ifndef ENABLE_AUTO_BUSOFF_RECOVERY
#define ENABLE_AUTO_BUSOFF_RECOVERY      0
#endif

/**
 * @brief Bus-Off Recovery Configuration
 */
#define BUSOFF_CHECK_INTERVAL_MS        5000
#define BUSOFF_RECOVERY_RETRY_COUNT     3
#define BUSOFF_RECOVERY_DELAY_MS        10
#define BUSOFF_ERROR_WARNING_THRESHOLD  64

/*==============================================================================
 * LOGGING CONFIGURATION
 *============================================================================*/

#define XGW_ENABLE_VERBOSE_LOGS         1       /* 0=minimal logs, 1=all logs */
#define XGW_ENABLE_TIMING_LOGS          0       /* Disable detailed timing logs */

/*==============================================================================
 * INCLUDES
 *============================================================================*/

#include "ccu_xgw_gateway.h"
#include "../ccu_log.h"
#include "../motor_mapping.h"
#include "../../gateway_shared.h"
#include "../common/xgw_protocol.h"
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include <drivers/uart.h>
#include <kernel/dpl/CacheP.h>
#include "kernel/dpl/DebugP.h"
#include "kernel/dpl/ClockP.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "task.h"
#include "FreeRTOS.h"

/*==============================================================================
 * INTERNAL DATA
 *============================================================================*/

/**
 * @brief Gateway configuration
 */
static xgw_gateway_config_t g_config = {
    .udp_rx_port = XGW_UDP_PORT_PC_TO_XGW,     /* 61904 */
    .udp_tx_port = XGW_UDP_PORT_XGW_TO_PC,     /* 53489 */
    .pc_ip_addr = IPADDR4_INIT_BYTES(192, 168, 1, 3),  /* PC IP - modify as needed */
    .tx_sequence = 0,
    .rx_sequence = 0
};

/**
 * @brief Gateway status
 */
static xgw_gateway_status_t g_status = {
    .initialized = false,
    .running = false,
    .udp_rx_count = 0,
    .udp_tx_count = 0,
    .parse_errors = 0,
    .crc_errors = 0,
    .motor_state_updates = 0,
    .imu_state_updates = 0
};

/**
 * @brief Task handles
 */
static TaskHandle_t g_udp_rx_task_handle = NULL;
static TaskHandle_t g_udp_tx_task_handle = NULL;

/**
 * @brief Queue handles
 */
static QueueHandle_t g_udp_rx_queue = NULL;

/**
 * @brief Task notification for UDP RX trigger
 */
static TaskHandle_t g_udp_rx_notify_task = NULL;

/**
 * @brief lwIP UDP PCB for receive
 */
static struct udp_pcb* g_udp_rx_pcb = NULL;

/**
 * @brief lwIP UDP PCB for transmit
 */
static struct udp_pcb* g_udp_tx_pcb = NULL;

/**
 * @brief Shared motor states buffer (read from IPC)
 *
 * This buffer is populated from IPC (Core1) and used to build UDP packets.
 */
typedef struct {
    xgw_motor_state_t state[VD1_NUM_MOTORS];
    uint32_t update_count;
    bool initialized;
} xgw_motor_states_t;

static xgw_motor_states_t g_motor_states = {0};

/**
 * @brief Shared IMU state buffer (read from IPC)
 */
typedef struct {
    xgw_imu_state_t state;
    uint32_t update_count;
    bool initialized;
} xgw_imu_states_t;

static xgw_imu_states_t g_imu_state = {0};

/*==============================================================================
 * INTERNAL FUNCTIONS
 *============================================================================*/

/**
 * @brief Get timestamp in nanoseconds
 *
 * Uses ClockP_getTimeUsec() for microsecond resolution timestamp
 * Returns nanoseconds (usec * 1000)
 */
static uint64_t get_timestamp_ns(void)
{
    uint64_t usec = ClockP_getTimeUsec();
    return usec * 1000ULL;
}

/**
 * @brief Calculate CRC32 for packet (uses crc32_core - matches Python's binascii.crc32)
 *
 * CRASH FIX (175s): Optimized to use smaller stack buffer.
 * Previous implementation used 4096-byte buffer on stack, causing UDP TX task
 * stack overflow at 500Hz operation. Reduced to only allocate what's needed.
 *
 * Maximum packet size: 32 (header) + 460 (23 motors * 20 bytes) = 492 bytes
 */
static uint32_t calculate_packet_crc32(const xgw_header_t* header, const void* payload, uint32_t payload_len)
{
    uint32_t total_len = sizeof(xgw_header_t) + payload_len;

    /*
     * CRASH FIX: Use appropriately sized buffer instead of fixed 4096 bytes.
     * Maximum motor state packet: 32 + 460 = 492 bytes
     * Maximum motor command packet: 32 + 552 = 584 bytes
     * Using 1024 bytes gives us headroom while avoiding excessive stack usage.
     */
    #define CRC_CALC_BUFFER_SIZE 1024  /* Was 4096 - causing stack overflow! */
    uint8_t temp_buffer[CRC_CALC_BUFFER_SIZE];

    if (total_len > CRC_CALC_BUFFER_SIZE)
    {
        xgw_error_log("[XGW] CRC calc: Packet too large: %u bytes\r\n", total_len);
        return 0; /* Return 0 to indicate error */
    }

    /* Create a temporary copy of the header on the stack to modify it safely */
    xgw_header_t temp_header;
    memcpy(&temp_header, header, sizeof(xgw_header_t));

    /* For CRC calculation, the crc32 and any padding fields must be zeroed */
    temp_header.crc32 = 0;
    temp_header.reserved_pad = 0;

    /* Copy the modified header into our main temporary buffer */
    memcpy(temp_buffer, &temp_header, sizeof(xgw_header_t));

    /* Copy the payload right after the header in the buffer */
    if (payload != NULL && payload_len > 0) {
        memcpy(temp_buffer + sizeof(xgw_header_t), payload, payload_len);
    }

    /* Calculate CRC on the prepared, contiguous buffer */
    uint32_t crc = crc32_core(temp_buffer, total_len);

    return crc;
}

/**
 * @brief Validate packet CRC32
 */
static bool validate_packet_crc32(const xgw_header_t* header, const void* payload, uint32_t payload_len)
{
    uint32_t calc_crc = calculate_packet_crc32(header, payload, payload_len);
    return (calc_crc == header->crc32);
}

/*==============================================================================
 * UDP RX TASK
 *============================================================================*/

/**
 * @brief UDP RX Task
 *
 * Processes UDP packets containing motor commands and sends to Core1 via IPC.
 *
 * [MODIFIED: Sends motor commands via IPC to Core1 instead of CAN TX queue]
 */
static void udp_rx_task(void* parameters)
{
    (void)parameters;
    xgw_udp_rx_item_t rx_item;
    uint32_t heartbeat = 0;
    uint32_t last_heartbeat_time = xTaskGetTickCount();

#if XGW_ENABLE_VERBOSE_LOGS
    xgw_debug_log("[XGW] UDP RX task started (port %d)\r\n", g_config.udp_rx_port);
#endif

    while (g_status.running) {
        heartbeat++;

#if XGW_ENABLE_TIMING_LOGS
        uint64_t wait_start = get_timestamp_ns();
#endif
        /* Wait for UDP RX notification (from lwIP callback) */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));

        /* DEFENSIVE: Check if queue is valid before using */
        if (g_udp_rx_queue == NULL) {
            xgw_error_log("[XGW] CRITICAL: UDP RX queue is NULL!\r\n");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Process all queued packets */
        while (xQueueReceive(g_udp_rx_queue, &rx_item, 0) == pdTRUE) {
            g_status.udp_rx_count++;

            /* Check minimum packet size (header only) */
            if (rx_item.length < sizeof(xgw_header_t)) {
                xgw_error_log("[XGW] UDP packet too small: %d bytes\r\n", rx_item.length);
                g_status.parse_errors++;
                continue;
            }

            /* Parse header */
            xgw_header_t header;
            memcpy(&header, rx_item.data, sizeof(xgw_header_t));

            /* Validate magic */
            if (header.magic != XGW_PROTOCOL_MAGIC) {
                xgw_error_log("[XGW] Invalid magic: 0x%04X\r\n", header.magic);
                g_status.parse_errors++;
                continue;
            }

            /* Validate version */
            if (header.version != XGW_PROTOCOL_VERSION) {
                xgw_error_log("[XGW] Unsupported version: %d\r\n", header.version);
                g_status.parse_errors++;
                continue;
            }

            /* Validate payload length */
            if (header.payload_len != (rx_item.length - sizeof(xgw_header_t))) {
                xgw_error_log("[XGW] Payload length mismatch: header=%d, actual=%d\r\n",
                             header.payload_len, rx_item.length - (int)sizeof(xgw_header_t));
                g_status.parse_errors++;
                continue;
            }

            /* Validate CRC32 */
            const void* payload = rx_item.data + sizeof(xgw_header_t);
            if (!validate_packet_crc32(&header, payload, header.payload_len)) {
                uint32_t calc_crc = calculate_packet_crc32(&header, payload, header.payload_len);
                xgw_error_log("[XGW] CRC32 validation failed: expected=0x%08X, calculated=0x%08X\r\n",
                             header.crc32, calc_crc);
                g_status.crc_errors++;
                continue;
            }

            /* Process by message type */
            int result = 0;
            switch (header.msg_type) {
                case XGW_MSG_TYPE_MOTOR_CMD:
                    result = xgw_process_motor_cmd(&header, payload, header.payload_len);
                    break;

                case XGW_MSG_TYPE_MOTOR_SET:
                    result = xgw_process_motor_set(&header, payload, header.payload_len);
                    break;

                case XGW_MSG_TYPE_XGW_CONFIG:
                    result = xgw_process_config(&header, payload, header.payload_len);
                    break;

                default:
                    xgw_error_log("[XGW] Unsupported message type: 0x%02X\r\n", header.msg_type);
                    g_status.parse_errors++;
                    break;
            }

            if (result != 0) {
                xgw_error_log("[XGW] Message processing failed: type=0x%02X, result=%d\r\n",
                             header.msg_type, result);
                g_status.parse_errors++;
            }
        }

        /* Log heartbeat every 5 seconds */
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - last_heartbeat_time) >= pdMS_TO_TICKS(5000)) {
#if XGW_ENABLE_VERBOSE_LOGS
            xgw_debug_log("[XGW] UDP RX heartbeat: cnt=%u, rx=%u, parse_err=%u\r\n",
                         heartbeat, g_status.udp_rx_count, g_status.parse_errors);
#endif
            last_heartbeat_time = current_time;
        }
    }

#if XGW_ENABLE_VERBOSE_LOGS
    xgw_debug_log("[XGW] UDP RX task stopped\r\n");
#endif
    vTaskDelete(NULL);
}

/*==============================================================================
 * UDP TX TASK
 *============================================================================*/

/**
 * @brief UDP TX Task
 *
 * Periodically (every 2ms) sends current motor and IMU states via UDP.
 *
 * [MODIFIED: Reads motor/IMU states from IPC (Core1) instead of shared buffer]
 */
static void udp_tx_task(void* parameters)
{
    (void)parameters;
    uint8_t tx_buffer[512];
    int packet_len;
    uint32_t status_counter = 3750;
    const uint32_t STATUS_INTERVAL_MS = 5000;

    /* For precise periodic timing with vTaskDelayUntil */
    TickType_t last_wake_time = xTaskGetTickCount();

    xgw_debug_log("[XGW] UDP TX task started (period: %d ms)\r\n", XGW_UDP_TX_PERIOD_MS);

    while (g_status.running) {
        /* Update motor states from IPC (Core1) */
        motor_state_ipc_t ipc_states[VD1_NUM_MOTORS];
        int count = gateway_read_motor_states(ipc_states);
        if (count > 0) {
            /* Copy to local buffer for UDP transmission */
            for (int i = 0; i < count && i < VD1_NUM_MOTORS; i++) {
                g_motor_states.state[i].motor_id = i;
                g_motor_states.state[i].position = ipc_states[i].position;
                g_motor_states.state[i].velocity = ipc_states[i].velocity;
                g_motor_states.state[i].torque = ipc_states[i].torque;
                g_motor_states.state[i].temp = ipc_states[i].temperature / 10.0f;
                g_motor_states.state[i].pattern = ipc_states[i].pattern;
                g_motor_states.state[i].error_code = ipc_states[i].error_code;
                g_motor_states.update_count++;
            }
            g_motor_states.initialized = true;
            g_status.motor_state_updates++;
        }

        /* Build and send Motor State packet */
        packet_len = xgw_build_motor_state_packet(tx_buffer, sizeof(tx_buffer));
        if (packet_len > 0) {
            struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, packet_len, PBUF_RAM);
            if (p != NULL) {
                memcpy(p->payload, tx_buffer, packet_len);

                LOCK_TCPIP_CORE();
                if (g_udp_tx_pcb != NULL) {
                    udp_sendto(g_udp_tx_pcb, p, &g_config.pc_ip_addr, g_config.udp_tx_port);
                    g_status.udp_tx_count++;
                }
                UNLOCK_TCPIP_CORE();

                pbuf_free(p);
            } else {
                g_status.parse_errors++;
            }
        }

#if CCU_IMU_ENABLED
        /* Update IMU state from IPC (Core1) */
        imu_state_ipc_t ipc_imu;
        if (gateway_read_imu_state(&ipc_imu) == 0) {
            /* Copy to local buffer for UDP transmission */
            g_imu_state.state.imu_id = ipc_imu.imu_id;
            g_imu_state.state.reserved = 0;
            g_imu_state.state.temp_cdeg = ipc_imu.temp_cdeg;
            memcpy(g_imu_state.state.gyro, ipc_imu.gyro, sizeof(g_imu_state.state.gyro));
            memcpy(g_imu_state.state.quat, ipc_imu.quat, sizeof(g_imu_state.state.quat));
            memcpy(g_imu_state.state.euler, ipc_imu.euler, sizeof(g_imu_state.state.euler));
            memset(g_imu_state.state.mag_val, 0, sizeof(g_imu_state.state.mag_val));
            memset(g_imu_state.state.mag_norm, 0, sizeof(g_imu_state.state.mag_norm));
            g_imu_state.update_count++;
            g_imu_state.initialized = true;
            g_status.imu_state_updates++;
        }

        /* Build and send IMU State packet */
        packet_len = xgw_build_imu_state_packet(tx_buffer, sizeof(tx_buffer));
        if (packet_len > 0) {
            struct pbuf* p_imu = pbuf_alloc(PBUF_TRANSPORT, packet_len, PBUF_RAM);
            if (p_imu != NULL) {
                memcpy(p_imu->payload, tx_buffer, packet_len);

                LOCK_TCPIP_CORE();
                if (g_udp_tx_pcb != NULL) {
                    udp_sendto(g_udp_tx_pcb, p_imu, &g_config.pc_ip_addr, g_config.udp_tx_port);
                    g_status.udp_tx_count++;
                }
                UNLOCK_TCPIP_CORE();

                pbuf_free(p_imu);
            }
        }
#endif

        /* Print status every 5 seconds */
        status_counter += XGW_UDP_TX_PERIOD_MS;
        if (status_counter >= STATUS_INTERVAL_MS) {
            xgw_debug_log("[XGW] Status: udp_rx=%d, udp_tx=%d, motor_upd=%d, imu_upd=%d, errors=%d\r\n",
                         g_status.udp_rx_count, g_status.udp_tx_count,
                         g_status.motor_state_updates, g_status.imu_state_updates,
                         g_status.parse_errors);

            ccu_log_dual("XGW", "Status: udp_rx=%d, udp_tx=%d, motor_upd=%d, imu_upd=%d, errors=%d",
                         g_status.udp_rx_count, g_status.udp_tx_count,
                         g_status.motor_state_updates, g_status.imu_state_updates,
                         g_status.parse_errors);

            status_counter = 0;
        }

        /* Wait for next period - using vTaskDelayUntil for precise timing */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(XGW_UDP_TX_PERIOD_MS));
    }

    xgw_debug_log("[XGW] UDP TX task stopped\r\n");
    vTaskDelete(NULL);
}

/**
 * @brief lwIP UDP receive callback
 */
static void udp_recv_callback(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                               const ip_addr_t* addr, u16_t port)
{
    (void)arg;

    if (p != NULL && g_status.running) {
        /* Queue the received data for processing */
        xgw_udp_rx_item_t rx_item;
        rx_item.length = (p->len > sizeof(rx_item.data)) ? sizeof(rx_item.data) : p->len;
        memcpy(rx_item.data, p->payload, rx_item.length);
        ip_addr_copy(rx_item.src_addr, *addr);
        rx_item.src_port = port;

        /* DEFENSIVE: Check if queue is valid before using */
        if (g_udp_rx_queue != NULL) {
            BaseType_t queue_result = xQueueSend(g_udp_rx_queue, &rx_item, 0);

            /* Only notify task if packet was successfully queued */
            if (queue_result == pdTRUE) {
                if (g_udp_rx_notify_task != NULL) {
                    BaseType_t higher_priority_task_woken = pdFALSE;
                    vTaskNotifyGiveFromISR(g_udp_rx_notify_task, &higher_priority_task_woken);
                    portYIELD_FROM_ISR(higher_priority_task_woken);
                }
            } else {
                /* Queue full - packet will be dropped */
                g_status.parse_errors++;
            }
        } else {
            g_status.parse_errors++;
        }
    }

    /* Free the pbuf */
    if (p != NULL) {
        pbuf_free(p);
    }
}

/*==============================================================================
 * PUBLIC FUNCTIONS - INITIALIZATION
 *============================================================================*/

int xgw_gateway_init(void)
{
    /* Initialize status */
    memset(&g_status, 0, sizeof(g_status));
    g_status.initialized = false;
    g_status.running = false;

    /* Initialize shared states */
    memset(&g_motor_states, 0, sizeof(g_motor_states));
    memset(&g_imu_state, 0, sizeof(g_imu_state));

    /* Create queues */
    g_udp_rx_queue = xQueueCreate(XGW_UDP_RX_QUEUE_SIZE, sizeof(xgw_udp_rx_item_t));
    if (g_udp_rx_queue == NULL) {
        xgw_error_log("[XGW] Failed to create UDP RX queue\r\n");
        return -1;
    }

    /* Initialize motor mapping shared memory access (Core0 waits for Core1) */
    if (motor_mapping_init_core0() != 0) {
        xgw_error_log("[XGW] Failed to initialize motor mapping\r\n");
        vQueueDelete(g_udp_rx_queue);
        return -1;
    }

    /* Wait for motor configuration from Core1 */
    if (gateway_wait_motor_config_ready(5000) != 0) {
        xgw_error_log("[XGW] Timeout waiting for motor configuration from Core1\r\n");
        vQueueDelete(g_udp_rx_queue);
        return -1;
    }

    g_status.initialized = true;
    xgw_debug_log("[XGW] Gateway initialized successfully (multicore mode)\r\n");

    return 0;
}

int xgw_gateway_start(void)
{
    if (!g_status.initialized || g_status.running) {
        return -1;
    }

    g_status.running = true;

    /* Create UDP RX task */
    BaseType_t result = xTaskCreate(
        udp_rx_task,
        "XGW_UDP_RX",
        XGW_TASK_STACK_SIZE_UDP_RX,
        NULL,
        XGW_TASK_PRIORITY_UDP_RX,
        &g_udp_rx_task_handle
    );
    if (result != pdPASS) {
        xgw_error_log("[XGW] Failed to create UDP RX task\r\n");
        g_status.running = false;
        return -1;
    }
    g_udp_rx_notify_task = g_udp_rx_task_handle;

    /* Create UDP TX task */
    result = xTaskCreate(
        udp_tx_task,
        "XGW_UDP_TX",
        XGW_TASK_STACK_SIZE_UDP_TX,
        NULL,
        XGW_TASK_PRIORITY_UDP_TX,
        &g_udp_tx_task_handle
    );
    if (result != pdPASS) {
        xgw_error_log("[XGW] Failed to create UDP TX task\r\n");
        g_status.running = false;
        vTaskDelete(g_udp_rx_task_handle);
        return -1;
    }

    xgw_debug_log("[XGW] Gateway tasks started (lwIP init pending)\r\n");
    ccu_log_dual("XGW", "Gateway tasks started - syslog test!");
    return 0;
}

/**
 * @brief Initialize lwIP UDP PCBs (must be called AFTER tcpip_init)
 */
int xgw_gateway_lwip_init(void)
{
    /* Setup UDP RX PCB */
    g_udp_rx_pcb = udp_new();
    if (g_udp_rx_pcb != NULL) {
        err_t err = udp_bind(g_udp_rx_pcb, IP_ADDR_ANY, g_config.udp_rx_port);
        if (err == ERR_OK) {
            udp_recv(g_udp_rx_pcb, udp_recv_callback, NULL);
            xgw_debug_log("[XGW] UDP RX bound to port %d\r\n", g_config.udp_rx_port);
        } else {
            xgw_error_log("[XGW] Failed to bind UDP RX to port %d\r\n", g_config.udp_rx_port);
        }
    }

    /* Setup UDP TX PCB */
    g_udp_tx_pcb = udp_new();
    if (g_udp_tx_pcb == NULL) {
        xgw_error_log("[XGW] Failed to create UDP TX PCB\r\n");
    }

    xgw_debug_log("[XGW] lwIP UDP PCBs initialized\r\n");
    return 0;
}

void xgw_gateway_stop(void)
{
    if (!g_status.running) {
        return;
    }

    g_status.running = false;

    /* Close UDP PCBs */
    if (g_udp_rx_pcb != NULL) {
        udp_remove(g_udp_rx_pcb);
        g_udp_rx_pcb = NULL;
    }
    if (g_udp_tx_pcb != NULL) {
        udp_remove(g_udp_tx_pcb);
        g_udp_tx_pcb = NULL;
    }

    /* Wait for tasks to finish */
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Reset all task handles and queue pointers */
    g_udp_rx_task_handle = NULL;
    g_udp_tx_task_handle = NULL;
    g_udp_rx_queue = NULL;
    g_udp_rx_pcb = NULL;
    g_udp_tx_pcb = NULL;
    g_udp_rx_notify_task = NULL;

    xgw_debug_log("[XGW] Gateway stopped\r\n");
}

void xgw_gateway_get_status(xgw_gateway_status_t* status)
{
    if (status != NULL) {
        *status = g_status;
    }
}

/*==============================================================================
 * PROTOCOL HANDLING FUNCTIONS
 *============================================================================*/

/**
 * @brief Process Motor Command packet (Type 0x02)
 *
 * [MODIFIED: Sends motor commands via IPC to Core1 instead of CAN TX queue]
 */
int xgw_process_motor_cmd(const xgw_header_t* header, const void* payload, uint32_t payload_len)
{
    if (header == NULL || payload == NULL) {
        return -1;
    }

    /* Validate payload size */
    uint32_t expected_size = header->count * sizeof(xgw_motor_cmd_t);
    if (payload_len != expected_size) {
        xgw_error_log("[XGW] Motor CMD payload size mismatch: expected %d, got %d\r\n",
                     expected_size, payload_len);
        return -1;
    }

    const xgw_motor_cmd_t* cmds = (const xgw_motor_cmd_t*)payload;

    /* Prepare IPC message for Core1 */
    motor_cmd_ipc_t ipc_cmds[VD1_NUM_MOTORS];
    uint32_t ipc_count = 0;

    /* Get motor configs from shared memory */
    for (uint8_t i = 0; i < header->count && i < VD1_NUM_MOTORS; i++) {
        const motor_config_t* config = motor_get_config(i);
        if (config == NULL) {
            continue;
        }

        /* Convert xGW motor command to IPC format */
        ipc_cmds[ipc_count].motor_id = config->motor_id;
        ipc_cmds[ipc_count].can_bus = config->can_bus;
        ipc_cmds[ipc_count].mode = 0;  /* Motion control mode */
        ipc_cmds[ipc_count].reserved = 0;
        ipc_cmds[ipc_count].position = (uint16_t)(cmds[i].position * 100.0f);  /* 0.01 rad */
        ipc_cmds[ipc_count].velocity = (int16_t)(cmds[i].velocity * 100.0f);  /* 0.01 rad/s */
        ipc_cmds[ipc_count].torque = (int16_t)(cmds[i].torque * 100.0f);     /* 0.01 Nm */
        ipc_cmds[ipc_count].kp = (uint16_t)(cmds[i].kp * 100.0f);            /* 0.01 */
        ipc_cmds[ipc_count].kd = (uint16_t)(cmds[i].kd * 100.0f);            /* 0.01 */
        ipc_count++;
    }

    /* Send to Core1 via IPC ring buffer */
    if (ipc_count > 0) {
        uint32_t bytes_written = 0;
        int result = gateway_ringbuf_core0_send(ipc_cmds,
                                                 ipc_count * sizeof(motor_cmd_ipc_t),
                                                 &bytes_written);
        if (result != GATEWAY_RINGBUF_OK) {
            xgw_error_log("[XGW] Failed to send motor commands to Core1: %d\r\n", result);
            g_status.parse_errors++;
            return -1;
        }

        /* Notify Core1 that data is ready */
        gateway_ringbuf_core0_notify();
    }

    return 0;
}

/**
 * @brief Process Motor Set packet (Type 0x07)
 *
 * [MODIFIED: Sends motor set commands via IPC to Core1]
 */
int xgw_process_motor_set(const xgw_header_t* header, const void* payload, uint32_t payload_len)
{
    if (header == NULL || payload == NULL) {
        return -1;
    }

    /* Validate payload size */
    if (payload_len != sizeof(xgw_motor_set_t)) {
        xgw_error_log("[XGW] Motor Set payload size mismatch\r\n");
        return -1;
    }

    const xgw_motor_set_t* motor_set = (const xgw_motor_set_t*)payload;

    /* FIX: motor_id in packet is index (0-22), not CAN motor ID */
    if (motor_set->motor_id >= VD1_NUM_MOTORS) {
        xgw_error_log("[XGW] Motor Set: motor_index %d out of range (0-%d)\r\n",
                      motor_set->motor_id, VD1_NUM_MOTORS - 1);
        return -1;
    }

    const motor_config_t* config = motor_get_config(motor_set->motor_id);
    if (config == NULL) {
        xgw_error_log("[XGW] Motor Set: motor_index %d config not found\r\n", motor_set->motor_id);
        return -1;
    }

    /* Determine communication type based on mode */
    uint8_t comm_type;
    const char* mode_str;

    switch (motor_set->mode) {
        case XGW_MOTOR_MODE_DISABLE:
            comm_type = COMM_TYPE_MOTOR_STOP;
            mode_str = "DISABLE";
            break;
        case XGW_MOTOR_MODE_ENABLE:
            comm_type = COMM_TYPE_MOTOR_ENABLE;
            mode_str = "ENABLE";
            break;
        case XGW_MOTOR_MODE_MECH_ZERO:
            comm_type = COMM_TYPE_SET_POS_ZERO;
            mode_str = "MECH_ZERO";
            break;
        case XGW_MOTOR_MODE_ZERO_STA:
            comm_type = COMM_TYPE_SET_POS_ZERO;
            mode_str = "ZERO_STA";
            break;
        case XGW_MOTOR_MODE_ZERO_STA_MECH:
            comm_type = COMM_TYPE_SET_POS_ZERO;
            mode_str = "ZERO_STA_MECH";
            break;
        default:
            xgw_error_log("[XGW] Motor Set: invalid mode %d\r\n", motor_set->mode);
            return -1;
    }

    /* Prepare IPC message for Core1 */
    motor_cmd_ipc_t ipc_cmd;
    ipc_cmd.motor_id = config->motor_id;
    ipc_cmd.can_bus = config->can_bus;
    ipc_cmd.mode = motor_set->mode;
    ipc_cmd.reserved = comm_type;  /* Store comm_type in reserved field */
    ipc_cmd.position = 0;
    ipc_cmd.velocity = 0;
    ipc_cmd.torque = 0;
    ipc_cmd.kp = 0;
    ipc_cmd.kd = 0;

    /* Send to Core1 via IPC ring buffer */
    uint32_t bytes_written = 0;
    int result = gateway_ringbuf_core0_send(&ipc_cmd, sizeof(ipc_cmd), &bytes_written);
    if (result != GATEWAY_RINGBUF_OK) {
        xgw_error_log("[XGW] Motor Set: Failed to send to Core1\r\n");
        return -1;
    }

    /* Notify Core1 that data is ready */
    gateway_ringbuf_core0_notify();

    xgw_debug_log("[XGW] Motor Set: idx=%d, can_id=%d, mode=%s (%d), bus=%d\r\n",
                 motor_set->motor_id, config->motor_id, mode_str, motor_set->mode,
                 config->can_bus);

    return 0;
}

/**
 * @brief Process Configuration packet (Type 0x06)
 */
int xgw_process_config(const xgw_header_t* header, const void* payload, uint32_t payload_len)
{
    if (header == NULL || payload == NULL) {
        return -1;
    }

    /* Validate payload size */
    if (payload_len != sizeof(xgw_config_t)) {
        xgw_error_log("[XGW] Config payload size mismatch\r\n");
        return -1;
    }

    const xgw_config_t* config = (const xgw_config_t*)payload;

    /* Handle configuration commands based on cmd_id */
    switch (config->cmd_id) {
        case 0:  /* GET Config - Request current configuration */
            xgw_debug_log("[XGW] Config: GET command received\r\n");
            /* TODO: Send configuration response via UDP */
            break;

        case 1:  /* SET Config - Apply configuration temporarily */
            xgw_debug_log("[XGW] Config: SET command (CAN baud=%d, RS485 baud=%d)\r\n",
                         config->can_baud, config->rs485_baud);
            break;

        case 2:  /* SAVE Config - Write to Flash */
            xgw_debug_log("[XGW] Config: SAVE command - write to flash\r\n");
            break;

        case 3:  /* REBOOT xGW */
            xgw_debug_log("[XGW] Config: REBOOT command received\r\n");
            break;

        default:
            xgw_error_log("[XGW] Config: unknown cmd_id=%d\r\n", config->cmd_id);
            return -1;
    }

    return 0;
}

/*==============================================================================
 * PACKET BUILDING FUNCTIONS
 *============================================================================*/

/**
 * @brief Build Motor State packet for UDP TX
 *
 * [MODIFIED: Reads motor states from IPC (Core1) instead of shared buffer]
 */
int xgw_build_motor_state_packet(uint8_t* buffer, uint32_t buffer_len)
{
    if (buffer == NULL || buffer_len < sizeof(xgw_header_t)) {
        return -1;
    }

    /* Check if motor states are initialized */
    if (!g_motor_states.initialized) {
        return -1;
    }

    /* Initialize header */
    xgw_header_t header;
    xgw_header_init(&header, XGW_MSG_TYPE_MOTOR_STATE, VD1_NUM_MOTORS,
                    VD1_NUM_MOTORS * sizeof(xgw_motor_state_t), g_config.tx_sequence++);

    /* Set timestamp using ClockP */
    header.timestamp_ns = get_timestamp_ns();

    /* Build payload */
    xgw_motor_state_t* motors = (xgw_motor_state_t*)(buffer + sizeof(xgw_header_t));

    /* Copy motor states from local buffer (populated from IPC) */
    for (uint8_t i = 0; i < VD1_NUM_MOTORS; i++) {
        motors[i].motor_id = i;
        motors[i].error_code = g_motor_states.state[i].error_code;
        motors[i].pattern = g_motor_states.state[i].pattern;
        motors[i].reserved = 0;
        motors[i].position = g_motor_states.state[i].position;
        motors[i].velocity = g_motor_states.state[i].velocity;
        motors[i].torque = g_motor_states.state[i].torque;
        motors[i].temp = g_motor_states.state[i].temp;
    }

    /* Calculate CRC */
    header.crc32 = calculate_packet_crc32(&header, motors, header.payload_len);

    /* Copy header to buffer */
    memcpy(buffer, &header, sizeof(xgw_header_t));

    return sizeof(xgw_header_t) + header.payload_len;
}

#if CCU_IMU_ENABLED
/**
 * @brief Build IMU State packet for UDP TX
 *
 * [MODIFIED: Reads IMU state from IPC (Core1) instead of shared buffer]
 */
int xgw_build_imu_state_packet(uint8_t* buffer, uint32_t buffer_len)
{
    if (buffer == NULL || buffer_len < sizeof(xgw_imu_state_t)) {
        return -1;
    }

    /* Check if IMU state is initialized */
    if (!g_imu_state.initialized) {
        return -1;
    }

    /* Initialize header */
    xgw_header_t header;
    xgw_header_init(&header, XGW_MSG_TYPE_IMU_STATE, 1,
                    sizeof(xgw_imu_state_t), g_config.tx_sequence++);

    /* Set timestamp using ClockP */
    header.timestamp_ns = get_timestamp_ns();

    /* Build payload */
    xgw_imu_state_t* imu = (xgw_imu_state_t*)(buffer + sizeof(xgw_header_t));

    /* Copy IMU state from local buffer (populated from IPC) */
    *imu = g_imu_state.state;

    /* Calculate CRC */
    header.crc32 = calculate_packet_crc32(&header, imu, header.payload_len);

    /* Copy header to buffer */
    memcpy(buffer, &header, sizeof(xgw_header_t));

    return sizeof(xgw_header_t) + header.payload_len;
}
#else
/* Stub function when IMU is disabled */
int xgw_build_imu_state_packet(uint8_t* buffer, uint32_t buffer_len)
{
    (void)buffer;
    (void)buffer_len;
    return 0;
}
#endif

/**
 * @brief Build Diagnostics packet for UDP TX
 */
int xgw_build_diag_packet(uint8_t* buffer, uint32_t buffer_len)
{
    if (buffer == NULL || buffer_len < sizeof(xgw_header_t)) {
        return -1;
    }

    /* Initialize header */
    xgw_header_t header;
    xgw_header_init(&header, XGW_MSG_TYPE_XGW_DIAG, 1,
                    sizeof(xgw_diag_t), g_config.tx_sequence++);

    /* Set timestamp using ClockP */
    header.timestamp_ns = get_timestamp_ns();

    /* Build payload */
    xgw_diag_t* diag = (xgw_diag_t*)(buffer + sizeof(xgw_header_t));

    diag->uptime_ms = xTaskGetTickCount();
    diag->voltage_in = 2400;  /* 24.0V - dummy */
    diag->current_in = 100;   /* 1.0A - dummy */
    diag->temp_mcu = 3500;    /* 35.00 C - dummy */
    diag->temp_pwr = 3000;    /* 30.00 C - dummy */
    diag->cpu_load = 30;
    diag->ram_usage = 20;
    diag->bus_load_can = 10;
    diag->bus_load_485 = 0;
    diag->status_flags = 0;
    memset(diag->reserved, 0, sizeof(diag->reserved));

    /* Calculate CRC */
    header.crc32 = calculate_packet_crc32(&header, diag, header.payload_len);

    /* Copy header to buffer */
    memcpy(buffer, &header, sizeof(xgw_header_t));

    return sizeof(xgw_header_t) + header.payload_len;
}

/*==============================================================================
 * DEBUG/LOG FUNCTIONS
 *============================================================================*/

/* xgw_debug_log and xgw_error_log are provided by ccu_log.h as macros
 * that map to ccu_log_debug() and ccu_log_error() with syslog support */
