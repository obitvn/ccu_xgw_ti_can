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
#include "../../gateway_shared.h"
#include "../common/crc32.h"
#include "kernel/dpl/ClockP.h"
#include "kernel/dpl/DebugP.h"
#include "kernel/dpl/AddrTranslateP.h"
#include "drivers/gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/api.h"
#include "lwip/udp.h"
#include "lwip/mem.h"
#include "lwip/sys.h"
#include <string.h>
#include "ti_drivers_config.h"

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define XGW_UDP_TASK_PRIORITY    (configMAX_PRIORITIES - 3)
#define XGW_UDP_TASK_STACK_SIZE   4096

/* [QA TRACE T019, T020] Debug GPIO pins for UDP RX/TX instrumentation */
#define DEBUG_GPIO_UDP_TX_BASE_ADDR      (CSL_GPIO3_U_BASE)
#define DEBUG_GPIO_UDP_TX_PIN            (60U)  /* GPIO3 PA4 = UDP TX indicator [QA TRACE T020] */
#define DEBUG_GPIO_UDP_RX_BASE_ADDR      (CSL_GPIO3_U_BASE)
#define DEBUG_GPIO_UDP_RX_PIN            (61U)  /* GPIO3 PA5 = UDP RX indicator [QA TRACE T019] */

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

/* PC IP address (default: broadcast - can be changed at runtime) */
static ip_addr_t g_pc_ip_addr = IPADDR4_INIT(0xFFFFFFFF);
static bool g_pc_ip_configured = false;  /* Track if IP was explicitly set */

/*==============================================================================
 * FORWARD DECLARATIONS
 *============================================================================*/

static void xgw_udp_recv_callback(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                                   const ip_addr_t* addr, u16_t port);

/* [QA TRACE T019, T020] Debug GPIO helper functions */
static void debug_gpio_init_udp(void);

/*==============================================================================
 * DEBUG GPIO HELPER FUNCTIONS
 *============================================================================*/

/**
 * @brief Initialize debug GPIO pins for UDP TX/RX instrumentation
 *
 * [QA TRACE T019, T020] Configure GPIO PA4 (TX) and PA5 (RX) as outputs
 * MUST be called before any UDP trace points
 */
static void debug_gpio_init_udp(void)
{
    uint32_t baseAddr;

    /* Configure PA4 as output (UDP TX indicator) [QA TRACE T020] */
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(DEBUG_GPIO_UDP_TX_BASE_ADDR);
    GPIO_setDirMode(baseAddr, DEBUG_GPIO_UDP_TX_PIN, GPIO_DIRECTION_OUTPUT);
    GPIO_pinWriteLow(baseAddr, DEBUG_GPIO_UDP_TX_PIN);

    /* Configure PA5 as output (UDP RX indicator) [QA TRACE T019] */
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(DEBUG_GPIO_UDP_RX_BASE_ADDR);
    GPIO_setDirMode(baseAddr, DEBUG_GPIO_UDP_RX_PIN, GPIO_DIRECTION_OUTPUT);
    GPIO_pinWriteLow(baseAddr, DEBUG_GPIO_UDP_RX_PIN);
}

/*==============================================================================
 * PUBLIC FUNCTIONS
 *============================================================================*/

int xgw_udp_init(void)
{
    memset(&g_udp_state, 0, sizeof(xgw_udp_state_t));

    /* [QA TRACE T019, T020] Initialize debug GPIO pins for UDP instrumentation */
    /* TODO: Enable GPIO3 PA4/PA5 in SysConfig before uncommenting
    debug_gpio_init_udp();
    */

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

    /* Create UDP RX PCB (called from tcpip thread context via lwip_init_callback) */
    g_udp_rx_pcb = udp_new();
    if (g_udp_rx_pcb == NULL) {
        DebugP_log("[xGW UDP] ERROR: Failed to create RX PCB!\r\n");
        return -1;
    }

    /* Bind to RX port */
    err_t err = udp_bind(g_udp_rx_pcb, IP_ADDR_ANY, XGW_UDP_RX_PORT);
    if (err != ERR_OK) {
        DebugP_log("[xGW UDP] ERROR: Failed to bind to port %d: %d\r\n", XGW_UDP_RX_PORT, err);
        udp_remove(g_udp_rx_pcb);
        g_udp_rx_pcb = NULL;
        return -1;
    }

    /* Set receive callback */
    udp_recv(g_udp_rx_pcb, xgw_udp_recv_callback, NULL);

    DebugP_log("[xGW UDP] RX PCB bound to port %d\r\n", XGW_UDP_RX_PORT);

    /* Create UDP TX PCB */
    g_udp_tx_pcb = udp_new();
    if (g_udp_tx_pcb == NULL) {
        DebugP_log("[xGW UDP] ERROR: Failed to create TX PCB!\r\n");
        udp_remove(g_udp_rx_pcb);
        g_udp_rx_pcb = NULL;
        return -1;
    }

    DebugP_log("[xGW UDP] TX PCB created\r\n");

    g_udp_state.started = true;
    DebugP_log("[xGW UDP] UDP interface started\r\n");
    return 0;
}

int xgw_udp_send_motor_states(const xgw_motor_state_t* states, uint8_t count)
{
    /* [QA TRACE T020] GPIO PA4 pulse on UDP TX entry */
    /* TODO: Enable GPIO instrumentation pins in SysConfig before uncommenting
    uint32_t baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(DEBUG_GPIO_UDP_TX_BASE_ADDR);
    GPIO_pinWriteHigh(baseAddr, DEBUG_GPIO_UDP_TX_PIN);
    */

    if (!g_udp_state.started || states == NULL || count == 0) {
        /* TODO: Enable GPIO instrumentation pins in SysConfig before uncommenting
        GPIO_pinWriteLow(baseAddr, DEBUG_GPIO_UDP_TX_PIN);
        */
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

    /* Send packet - MUST lock tcpip core when calling from non-tcpip thread */
    LOCK_TCPIP_CORE();
    err_t err = udp_sendto(g_udp_tx_pcb, p, &g_pc_ip_addr, XGW_UDP_TX_PORT);
    UNLOCK_TCPIP_CORE();

    /* Free pbuf */
    pbuf_free(p);

    /* [QA TRACE T020] GPIO PA4 LOW on UDP TX exit */
    /* TODO: Enable GPIO instrumentation pins in SysConfig before uncommenting
    GPIO_pinWriteLow(baseAddr, DEBUG_GPIO_UDP_TX_PIN);
    */

    if (err == ERR_OK) {
        g_udp_state.tx_count++;
        return total_len;
    } else {
        g_udp_state.tx_errors++;
        return -1;
    }
}

int xgw_udp_send_imu_state(const xgw_imu_state_t* imu_state)
{
    if (!g_udp_state.started || imu_state == NULL) {
        return -1;
    }

    /* Allocate pbuf for packet */
    uint16_t payload_len = sizeof(xgw_imu_state_t);
    uint16_t total_len = sizeof(xgw_header_t) + payload_len;
    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, total_len, PBUF_RAM);

    if (p == NULL) {
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

    /* Send packet - MUST lock tcpip core when calling from non-tcpip thread */
    LOCK_TCPIP_CORE();
    err_t err = udp_sendto(g_udp_tx_pcb, p, &g_pc_ip_addr, XGW_UDP_TX_PORT);
    UNLOCK_TCPIP_CORE();

    /* Free pbuf */
    pbuf_free(p);

    if (err == ERR_OK) {
        g_udp_state.tx_count++;
        return total_len;
    } else {
        g_udp_state.tx_errors++;
        return -1;
    }
}

int xgw_udp_send_diagnostics(const xgw_diag_t* diag)
{
    if (!g_udp_state.started || diag == NULL) {
        return -1;
    }

    /* Allocate pbuf for packet */
    uint16_t payload_len = sizeof(xgw_diag_t);
    uint16_t total_len = sizeof(xgw_header_t) + payload_len;
    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, total_len, PBUF_RAM);

    if (p == NULL) {
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

    /* Send packet - MUST lock tcpip core when calling from non-tcpip thread */
    LOCK_TCPIP_CORE();
    err_t err = udp_sendto(g_udp_tx_pcb, p, &g_pc_ip_addr, XGW_UDP_TX_PORT);
    UNLOCK_TCPIP_CORE();

    /* Free pbuf */
    pbuf_free(p);

    if (err == ERR_OK) {
        g_udp_state.tx_count++;
        return total_len;
    } else {
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
    return g_udp_state.started;
}

/**
 * @brief Set PC IP address for UDP TX
 *
 * @param ip_addr Pointer to IP address (4 bytes for IPv4)
 * @return 0 on success, -1 on error
 */
int xgw_udp_set_pc_ip(const uint8_t* ip_addr)
{
    if (ip_addr == NULL) {
        return -1;
    }

    IP4_ADDR(&g_pc_ip_addr, ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3]);
    g_pc_ip_configured = true;

    DebugP_log("[xGW UDP] PC IP set to %u.%u.%u.%u\r\n",
               ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3]);
    return 0;
}

/**
 * @brief Get current PC IP address
 *
 * @param ip_addr Pointer to store IP address (4 bytes)
 * @return 0 on success, -1 on error
 */
int xgw_udp_get_pc_ip(uint8_t* ip_addr)
{
    if (ip_addr == NULL) {
        return -1;
    }

    ip_addr[0] = ip4_addr_get_u32(&g_pc_ip_addr) & 0xFF;
    ip_addr[1] = (ip4_addr_get_u32(&g_pc_ip_addr) >> 8) & 0xFF;
    ip_addr[2] = (ip4_addr_get_u32(&g_pc_ip_addr) >> 16) & 0xFF;
    ip_addr[3] = (ip4_addr_get_u32(&g_pc_ip_addr) >> 24) & 0xFF;

    return 0;
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

    /* Write to shared memory via ring buffer */
    uint32_t bytes_written = 0;
    int ret = gateway_ringbuf_core0_send(ipc_cmds, count * sizeof(motor_cmd_ipc_t), &bytes_written);

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

    if (count > XGW_MAX_MOTORS) {
        DebugP_log("[xGW UDP] ERROR: Too many motor sets: %d\r\n", count);
        g_udp_state.parse_errors++;
        return -1;
    }

    /* Convert motor set commands to motor commands and send to Core1
     * Motor set commands are used to configure motor mode (disable, enable, etc)
     * The motor_id will be used to look up the CAN bus from motor_config table */
    motor_cmd_ipc_t ipc_cmds[XGW_MAX_MOTORS];

    for (uint8_t i = 0; i < count; i++) {
        ipc_cmds[i].motor_id = sets[i].motor_id;
        /* can_bus will be determined by motor_id lookup in Core1 */
        ipc_cmds[i].can_bus = 0;  /* Will be filled by Core1 based on motor_id */
        ipc_cmds[i].mode = sets[i].mode;
        ipc_cmds[i].reserved = 0;
        ipc_cmds[i].position = 0;  /* Motor set doesn't include position */
        ipc_cmds[i].velocity = 0;  /* Motor set doesn't include velocity */
        ipc_cmds[i].torque = 0;    /* Motor set doesn't include torque */
        ipc_cmds[i].kp = 0;        /* Motor set doesn't include Kp */
        ipc_cmds[i].kd = 0;        /* Motor set doesn't include Kd */
    }

    /* Write to shared memory via ring buffer */
    uint32_t bytes_written = 0;
    int ret = gateway_ringbuf_core0_send(ipc_cmds, count * sizeof(motor_cmd_ipc_t), &bytes_written);

    if (ret == 0) {
        /* Notify Core 1 */
        gateway_notify_commands_ready();
        g_udp_state.rx_count++;
        DebugP_log("[xGW UDP] Processed %d motor set commands\r\n", count);
        return 0;
    } else {
        DebugP_log("[xGW UDP] ERROR: Failed to write motor set commands!\r\n");
        g_udp_state.rx_errors++;
        return -1;
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

    /* [DEBUG-UDP-RX] Packet received */
    DebugP_log("[xGW UDP] RX callback: p=%p, len=%d, port=%d\r\n", p, p ? p->len : 0, port);

    /* [QA TRACE T019] GPIO PA5 pulse on UDP RX entry */
    /* TODO: Enable GPIO instrumentation pins in SysConfig before uncommenting
    uint32_t baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(DEBUG_GPIO_UDP_RX_BASE_ADDR);
    GPIO_pinWriteHigh(baseAddr, DEBUG_GPIO_UDP_RX_PIN);
    */

    if (p == NULL) {
        /* TODO: Enable GPIO instrumentation pins in SysConfig before uncommenting
        GPIO_pinWriteLow(baseAddr, DEBUG_GPIO_UDP_RX_PIN);
        */
        DebugP_log("[xGW UDP] RX: p=NULL, returning\r\n");
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
                        g_udp_state.rx_count++;
                        /* [QA TRACE T027] Increment UDP RX counter */
                        DEBUG_COUNTER_INC(dbg_udp_rx_count);
                        break;

                    case XGW_MSG_TYPE_MOTOR_SET:
                        xgw_udp_process_motor_set(data, length);
                        g_udp_state.rx_count++;
                        /* [QA TRACE T027] Increment UDP RX counter */
                        DEBUG_COUNTER_INC(dbg_udp_rx_count);
                        break;

                    default:
                        DebugP_log("[xGW UDP] Unknown message type: 0x%02X\r\n", header->msg_type);
                        g_udp_state.parse_errors++;
                        break;
                }
            } else {
                DebugP_log("[xGW UDP] RX: Packet too short: %d < %d\r\n", length, sizeof(xgw_header_t));
            }
            /* NOTE: User callback (g_rx_callback) removed to prevent duplicate processing.
             * All UDP packet processing is now handled directly in this callback. */
        } else {
            DebugP_log("[xGW UDP] RX: Wrong port: %d (expected %d)\r\n", port, XGW_UDP_RX_PORT);
        }
    } else {
        DebugP_log("[xGW UDP] RX: Invalid length: %d\r\n", p ? p->len : 0);
    }

    /* Free pbuf */
    pbuf_free(p);

    /* [QA TRACE T019] GPIO PA5 LOW on UDP RX exit */
    /* TODO: Enable GPIO instrumentation pins in SysConfig before uncommenting
    GPIO_pinWriteLow(baseAddr, DEBUG_GPIO_UDP_RX_PIN);
    */
}
