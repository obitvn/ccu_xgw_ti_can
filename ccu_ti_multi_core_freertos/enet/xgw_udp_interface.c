/**
 * @file xgw_udp_interface.c
 * @brief xGW UDP Interface Implementation for Core 0 (FreeRTOS)
 *
 * Simplified UDP interface for xGW protocol communication
 * Uses lwIP stack for UDP communication
 *
 * @author CCU Multicore Project
 * @date 2026-03-24
 */

#include "xgw_udp_interface.h"
#include "../gateway_shared.h"
#include "kernel/dpl/DebugP.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/api.h"
#include "lwip/udp.h"
#include "lwip/mem.h"
#include "../common/crc32.h"
#include <string.h>

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define XGW_UDP_TASK_PRIORITY    (configMAX_PRIORITIES - 3)
#define XGW_UDP_TASK_STACK_SIZE   4096

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

/**
 * @brief UDP packet buffer
 */
typedef struct {
    uint8_t data[XGW_UDP_MAX_PACKET_SIZE];
    uint16_t length;
    ip_addr_t src_addr;
    u16_t src_port;
} xgw_udp_packet_t;

/*==============================================================================
 * GLOBAL VARIABLES
 *============================================================================*/

/* UDP interface state */
static xgw_udp_state_t g_udp_state = {0};

/* UDP PCBs */
static struct udp_pcb* g_udp_rx_pcb = NULL;
static struct udp_pcb* g_udp_tx_pcb = NULL;

/* RX callback */
static xgw_udp_rx_callback_t g_rx_callback = NULL;

/* Task handle */
static TaskHandle_t g_udp_task_handle = NULL;

/* PC IP address (default: broadcast) */
static ip_addr_t g_pc_ip_addr = IPADDR4_INIT(0xFFFFFFFF);

/*==============================================================================
 * FORWARD DECLARATIONS
 *============================================================================*/

static void xgw_udp_rx_task(void* args);
static void xgw_udp_recv_callback(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                                   const ip_addr_t* addr, u16_t port);

/*==============================================================================
 * PUBLIC FUNCTIONS
 *============================================================================*/

int xgw_udp_init(void)
{
    memset(&g_udp_state, 0, sizeof(xgw_udp_state_t));

    DebugP_log("[xGW UDP] Initializing UDP interface...\r\n");
    DebugP_log("[xGW UDP] RX Port: %d, TX Port: %d\r\n", XGW_UDP_RX_PORT, XGW_UDP_TX_PORT);

    g_udp_state.initialized = true;
    g_udp_state.sequence = 0;

    return 0;
}

int xgw_udp_start(void)
{
    if (!g_udp_state.initialized) {
        DebugP_log("[xGW UDP] ERROR: Not initialized!\r\n");
        return -1;
    }

    DebugP_log("[xGW UDP] Starting UDP interface...\r\n");

    /* Create UDP RX task */
    BaseType_t ret = xTaskCreate(
        xgw_udp_rx_task,
        "xgw_udp_rx",
        XGW_UDP_TASK_STACK_SIZE,
        NULL,
        XGW_UDP_TASK_PRIORITY,
        &g_udp_task_handle
    );

    if (ret != pdPASS) {
        DebugP_log("[xGW UDP] ERROR: Failed to create RX task!\r\n");
        return -1;
    }

    DebugP_log("[xGW UDP] UDP interface started\r\n");
    return 0;
}

int xgw_udp_send_motor_states(const xgw_motor_state_t* states, uint8_t count)
{
    if (!g_udp_state.initialized || states == NULL || count == 0) {
        return -1;
    }

    if (count > XGW_MAX_MOTORS) {
        count = XGW_MAX_MOTORS;
    }

    /* Allocate pbuf for packet */
    uint16_t payload_len = count * sizeof(xgw_motor_state_t);
    uint16_t total_len = sizeof(xgw_header_t) + payload_len;
    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, total_len, PBUF_RAM);

    if (p == NULL) {
        DebugP_log("[xGW UDP] ERROR: Failed to allocate pbuf!\r\n");
        g_udp_state.tx_errors++;
        return -1;
    }

    /* Build packet */
    uint8_t* data = (uint8_t*)p->payload;

    /* Initialize header */
    xgw_header_t* header = (xgw_header_t*)data;
    xgw_header_init(header, XGW_MSG_TYPE_MOTOR_STATE, count, payload_len, g_udp_state.sequence++);

    /* Set timestamp */
    header->timestamp_ns = ClockP_getTimeUsec() * 1000ULL;

    /* Copy motor states */
    memcpy(data + sizeof(xgw_header_t), states, payload_len);

    /* Calculate CRC */
    header->crc32 = xgw_crc32_calculate(header, data + sizeof(xgw_header_t), payload_len);

    /* Send packet */
    err_t err = udp_sendto(g_udp_tx_pcb, p, &g_pc_ip_addr, XGW_UDP_TX_PORT);

    /* Free pbuf */
    pbuf_free(p);

    if (err == ERR_OK) {
        g_udp_state.tx_count++;
        return total_len;
    } else {
        DebugP_log("[xGW UDP] ERROR: udp_sendto failed: %d\r\n", err);
        g_udp_state.tx_errors++;
        return -1;
    }
}

int xgw_udp_send_imu_state(const xgw_imu_state_t* imu_state)
{
    if (!g_udp_state.initialized || imu_state == NULL) {
        return -1;
    }

    /* Allocate pbuf for packet */
    uint16_t payload_len = sizeof(xgw_imu_state_t);
    uint16_t total_len = sizeof(xgw_header_t) + payload_len;
    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, total_len, PBUF_RAM);

    if (p == NULL) {
        DebugP_log("[xGW UDP] ERROR: Failed to allocate pbuf for IMU!\r\n");
        g_udp_state.tx_errors++;
        return -1;
    }

    /* Build packet */
    uint8_t* data = (uint8_t*)p->payload;

    /* Initialize header */
    xgw_header_t* header = (xgw_header_t*)data;
    xgw_header_init(header, XGW_MSG_TYPE_IMU_STATE, 1, payload_len, g_udp_state.sequence++);

    /* Set timestamp */
    header->timestamp_ns = ClockP_getTimeUsec() * 1000ULL;

    /* Copy IMU state */
    memcpy(data + sizeof(xgw_header_t), imu_state, payload_len);

    /* Calculate CRC */
    header->crc32 = xgw_crc32_calculate(header, data + sizeof(xgw_header_t), payload_len);

    /* Send packet */
    err_t err = udp_sendto(g_udp_tx_pcb, p, &g_pc_ip_addr, XGW_UDP_TX_PORT);

    /* Free pbuf */
    pbuf_free(p);

    if (err == ERR_OK) {
        g_udp_state.tx_count++;
        return total_len;
    } else {
        DebugP_log("[xGW UDP] ERROR: udp_sendto failed for IMU: %d\r\n", err);
        g_udp_state.tx_errors++;
        return -1;
    }
}

int xgw_udp_send_diagnostics(const xgw_diag_t* diag)
{
    if (!g_udp_state.initialized || diag == NULL) {
        return -1;
    }

    /* Allocate pbuf for packet */
    uint16_t payload_len = sizeof(xgw_diag_t);
    uint16_t total_len = sizeof(xgw_header_t) + payload_len;
    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, total_len, PBUF_RAM);

    if (p == NULL) {
        DebugP_log("[xGW UDP] ERROR: Failed to allocate pbuf for diag!\r\n");
        g_udp_state.tx_errors++;
        return -1;
    }

    /* Build packet */
    uint8_t* data = (uint8_t*)p->payload;

    /* Initialize header */
    xgw_header_t* header = (xgw_header_t*)data;
    xgw_header_init(header, XGW_MSG_TYPE_XGW_DIAG, 1, payload_len, g_udp_state.sequence++);

    /* Set timestamp */
    header->timestamp_ns = ClockP_getTimeUsec() * 1000ULL;

    /* Copy diagnostics */
    memcpy(data + sizeof(xgw_header_t), diag, payload_len);

    /* Calculate CRC */
    header->crc32 = xgw_crc32_calculate(header, data + sizeof(xgw_header_t), payload_len);

    /* Send packet */
    err_t err = udp_sendto(g_udp_tx_pcb, p, &g_pc_ip_addr, XGW_UDP_TX_PORT);

    /* Free pbuf */
    pbuf_free(p);

    if (err == ERR_OK) {
        g_udp_state.tx_count++;
        return total_len;
    } else {
        DebugP_log("[xGW UDP] ERROR: udp_sendto failed for diag: %d\r\n", err);
        g_udp_state.tx_errors++;
        return -1;
    }
}

int xgw_udp_register_rx_callback(xgw_udp_rx_callback_t callback)
{
    g_rx_callback = callback;
    return 0;
}

void xgw_udp_get_state(xgw_udp_state_t* state)
{
    if (state != NULL) {
        *state = g_udp_state;
    }
}

bool xgw_udp_is_initialized(void)
{
    return g_udp_state.initialized;
}

/*==============================================================================
 * PACKET PROCESSING
 *============================================================================*/

int xgw_udp_process_motor_cmd(const uint8_t* data, uint16_t length)
{
    if (data == NULL || length < sizeof(xgw_header_t)) {
        return -1;
    }

    /* Parse header */
    const xgw_header_t* header = (const xgw_header_t*)data;

    /* Validate magic */
    if (header->magic != XGW_PROTOCOL_MAGIC) {
        DebugP_log("[xGW UDP] ERROR: Invalid magic: 0x%04X\r\n", header->magic);
        g_udp_state.parse_errors++;
        return -1;
    }

    /* Validate CRC */
    if (!xgw_crc32_validate(header, data + sizeof(xgw_header_t))) {
        DebugP_log("[xGW UDP] ERROR: CRC validation failed!\r\n");
        g_udp_state.crc_errors++;
        return -1;
    }

    /* Check message type */
    if (header->msg_type != XGW_MSG_TYPE_MOTOR_CMD) {
        DebugP_log("[xGW UDP] ERROR: Expected MOTOR_CMD (0x%02X), got 0x%02X\r\n",
                   XGW_MSG_TYPE_MOTOR_CMD, header->msg_type);
        g_udp_state.parse_errors++;
        return -1;
    }

    /* Extract motor commands */
    const xgw_motor_cmd_t* cmds = (const xgw_motor_cmd_t*)(data + sizeof(xgw_header_t));
    uint8_t count = header->count;

    if (count > XGW_MAX_MOTORS) {
        DebugP_log("[xGW UDP] ERROR: Too many motors: %d\r\n", count);
        g_udp_state.parse_errors++;
        return -1;
    }

    /* Convert to IPC format and write to shared memory */
    motor_cmd_ipc_t ipc_cmds[XGW_MAX_MOTORS];

    for (uint8_t i = 0; i < count; i++) {
        ipc_cmds[i].motor_id = cmds[i].motor_id;
        ipc_cmds[i].mode = cmds[i].mode;
        ipc_cmds[i].position = (uint16_t)(cmds[i].position * 100);  /* rad -> 0.01 rad */
        ipc_cmds[i].velocity = (int16_t)(cmds[i].velocity * 100);   /* rad/s -> 0.01 rad/s */
        ipc_cmds[i].torque = (int16_t)(cmds[i].torque * 100);      /* Nm -> 0.01 Nm */
        ipc_cmds[i].kp = (uint16_t)(cmds[i].kp * 100);             /* -> 0.01 */
        ipc_cmds[i].kd = (uint16_t)(cmds[i].kd * 100);             /* -> 0.01 */
    }

    /* Write to shared memory */
    int ret = gateway_write_motor_commands(ipc_cmds, count);

    if (ret == 0) {
        /* Notify Core 1 */
        gateway_notify_commands_ready();
        g_udp_state.rx_count++;
        return 0;
    } else {
        DebugP_log("[xGW UDP] ERROR: Failed to write motor commands!\r\n");
        g_udp_state.rx_errors++;
        return -1;
    }
}

int xgw_udp_process_motor_set(const uint8_t* data, uint16_t length)
{
    if (data == NULL || length < sizeof(xgw_header_t)) {
        return -1;
    }

    /* Parse header */
    const xgw_header_t* header = (const xgw_header_t*)data;

    /* Validate magic */
    if (header->magic != XGW_PROTOCOL_MAGIC) {
        g_udp_state.parse_errors++;
        return -1;
    }

    /* Validate CRC */
    if (!xgw_crc32_validate(header, data + sizeof(xgw_header_t))) {
        g_udp_state.crc_errors++;
        return -1;
    }

    /* Check message type */
    if (header->msg_type != XGW_MSG_TYPE_MOTOR_SET) {
        g_udp_state.parse_errors++;
        return -1;
    }

    /* Extract motor set commands */
    const xgw_motor_set_t* sets = (const xgw_motor_set_t*)(data + sizeof(xgw_header_t));
    uint8_t count = header->count;

    /* TODO: Process motor set commands */
    DebugP_log("[xGW UDP] Received %d motor set commands\r\n", count);

    g_udp_state.rx_count++;
    return 0;
}

/*==============================================================================
 * UDP RX TASK
 *============================================================================*/

static void xgw_udp_rx_task(void* args)
{
    (void)args;

    DebugP_log("[xGW UDP] RX task started\r\n");

    /* Create UDP RX PCB */
    g_udp_rx_pcb = udp_new();

    if (g_udp_rx_pcb == NULL) {
        DebugP_log("[xGW UDP] ERROR: Failed to create RX PCB!\r\n");
        vTaskDelete(NULL);
        return;
    }

    /* Bind to RX port */
    err_t err = udp_bind(g_udp_rx_pcb, IP_ADDR_ANY, XGW_UDP_RX_PORT);

    if (err != ERR_OK) {
        DebugP_log("[xGW UDP] ERROR: Failed to bind to port %d: %d\r\n", XGW_UDP_RX_PORT, err);
        udp_remove(g_udp_rx_pcb);
        vTaskDelete(NULL);
        return;
    }

    /* Set receive callback */
    udp_recv(g_udp_rx_pcb, xgw_udp_recv_callback, NULL);

    DebugP_log("[xGW UDP] Listening on port %d\r\n", XGW_UDP_RX_PORT);

    /* Create UDP TX PCB */
    g_udp_tx_pcb = udp_new();

    if (g_udp_tx_pcb == NULL) {
        DebugP_log("[xGW UDP] ERROR: Failed to create TX PCB!\r\n");
        vTaskDelete(NULL);
        return;
    }

    /* Main loop - task handles callbacks via lwIP */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/*==============================================================================
 * UDP RX CALLBACK
 *============================================================================*/

static void xgw_udp_recv_callback(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                                   const ip_addr_t* addr, u16_t port)
{
    (void)arg;
    (void)pcb;

    if (p == NULL) {
        return;
    }

    /* Copy packet data */
    if (p->len > 0 && p->len <= XGW_UDP_MAX_PACKET_SIZE) {
        uint8_t data[XGW_UDP_MAX_PACKET_SIZE];
        uint16_t length = pbuf_copy_partial(p, data, p->len, 0);

        /* Process packet based on source port */
        if (port == XGW_UDP_RX_PORT) {
            /* Check message type */
            if (length >= sizeof(xgw_header_t)) {
                const xgw_header_t* header = (const xgw_header_t*)data;

                switch (header->msg_type) {
                    case XGW_MSG_TYPE_MOTOR_CMD:
                        xgw_udp_process_motor_cmd(data, length);
                        break;

                    case XGW_MSG_TYPE_MOTOR_SET:
                        xgw_udp_process_motor_set(data, length);
                        break;

                    default:
                        DebugP_log("[xGW UDP] Unknown message type: 0x%02X\r\n", header->msg_type);
                        g_udp_state.parse_errors++;
                        break;
                }
            }

            /* Call user callback if registered */
            if (g_rx_callback != NULL) {
                g_rx_callback(data, length, (const uint8_t*)addr, port);
            }
        }
    }

    /* Free pbuf */
    pbuf_free(p);
}
