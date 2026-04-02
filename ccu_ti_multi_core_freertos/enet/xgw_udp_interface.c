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
#include "lwip/tcpip.h"
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

/* [DEBUG B029] Pbuf tracking counters - visible for main.c stats */
volatile uint32_t g_pbuf_alloc_count = 0;
volatile uint32_t g_pbuf_free_count = 0;
volatile uint32_t g_pbuf_alloc_fail_count = 0;
volatile uint32_t g_udp_sendto_count = 0;

/* UDP PCBs */
static struct udp_pcb* g_udp_rx_pcb = NULL;
static struct udp_pcb* g_udp_tx_pcb = NULL;

/* RX callback */
static xgw_udp_rx_callback_t g_rx_callback = NULL;

/* Task handle */
static TaskHandle_t g_udp_task_handle = NULL;

/* PC IP address (default: broadcast - can be changed at runtime) */
/* [FIX B029] Default PC IP address - use unicast instead of broadcast
 * Broadcast (255.255.255.255) can cause pbuf reference counting issues
 * Default: 192.168.1.3 (same as ccu_ti reference)
 * User can override via xgw_udp_set_pc_ip() */
static ip_addr_t g_pc_ip_addr = IPADDR4_INIT_BYTES(192, 168, 1, 3);
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

    /* Prevent multiple calls - already started */
    if (g_udp_state.started) {
        DebugP_log("[xGW UDP] Already started, skipping...\r\n");
        return 0;
    }

    DebugP_log("[xGW UDP] Starting UDP interface...\r\n");

    /* Create UDP RX PCB (called from tcpip thread context via lwip_init_callback) */
    g_udp_rx_pcb = udp_new();
    if (g_udp_rx_pcb == NULL) {
        DebugP_log("[xGW UDP] ERROR: Failed to create RX PCB!\r\n");
        return -1;
    }

    /* Bind to RX port
     * lwIP udp_bind() expects port in HOST byte order and will call htons() internally
     * Reference: ccu_ti source code uses direct port value without conversion */
    DebugP_log("[xGW UDP] Attempting to bind RX PCB to port %d (0x%X)...\r\n",
               XGW_UDP_RX_PORT, XGW_UDP_RX_PORT);
    err_t err = udp_bind(g_udp_rx_pcb, IP_ADDR_ANY, (u16_t)XGW_UDP_RX_PORT);
    if (err != ERR_OK) {
        DebugP_log("[xGW UDP] ERROR: Failed to bind to port %d: %d\r\n", XGW_UDP_RX_PORT, err);
        udp_remove(g_udp_rx_pcb);
        g_udp_rx_pcb = NULL;
        return -1;
    }

    /* Verify bind succeeded by checking actual port */
    uint16_t actual_port = lwip_ntohs(g_udp_rx_pcb->local_port);
    DebugP_log("[xGW UDP] RX PCB bound: requested=%d (0x%X), actual=%d (0x%X), local_port_raw=0x%X\r\n",
               XGW_UDP_RX_PORT, XGW_UDP_RX_PORT,
               actual_port, actual_port,
               g_udp_rx_pcb->local_port);

    /* Set receive callback */
    udp_recv(g_udp_rx_pcb, xgw_udp_recv_callback, NULL);

    DebugP_log("[xGW UDP] RX PCB setup complete, pcb=%p\r\n", g_udp_rx_pcb);

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

    if (g_udp_tx_pcb == NULL) {
        static uint32_t null_pcb_log_count = 0;
        if (null_pcb_log_count < 3) {
            DebugP_log("[xGW UDP] Motor: TX PCB is NULL!\r\n");
            null_pcb_log_count++;
        }
        return -1;
    }

    /* Debug: Log target IP and port (first time only) */
    static bool target_logged = false;
    if (!target_logged) {
        uint32_t ip_val = ip4_addr_get_u32(&g_pc_ip_addr);
        DebugP_log("[xGW UDP] Motor: Target IP=%lu.%lu.%lu.%lu, port=%u\r\n",
                   ip_val & 0xFF, (ip_val >> 8) & 0xFF, (ip_val >> 16) & 0xFF, (ip_val >> 24) & 0xFF,
                   XGW_UDP_TX_PORT);
        target_logged = true;
    }

    if (count > XGW_MAX_MOTORS) {
        count = XGW_MAX_MOTORS;
    }

    /* Allocate pbuf for packet */
    uint16_t payload_len = count * sizeof(xgw_motor_state_t);
    uint16_t total_len = sizeof(xgw_header_t) + payload_len;
    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, total_len, PBUF_RAM);

    if (p == NULL) {
        g_pbuf_alloc_fail_count++;
        DebugP_log("[xGW UDP] Motor: pbuf_alloc FAILED! size=%u, alloc_cnt=%u, free_cnt=%u, fail_cnt=%u\r\n",
                   total_len, g_pbuf_alloc_count, g_pbuf_free_count, g_pbuf_alloc_fail_count);
        g_udp_state.tx_errors++;
        return -1;
    }

    g_pbuf_alloc_count++;

    /* Debug: Log pbuf allocation success (first 5 times only) */
    static uint32_t alloc_log_count = 0;
    if (alloc_log_count < 5) {
        DebugP_log("[xGW UDP] Motor: pbuf_alloc OK, len=%u, pbuf=%p, alloc_cnt=%u\r\n", total_len, p, g_pbuf_alloc_count);
        alloc_log_count++;
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

    g_udp_sendto_count++;

    /* [DEBUG] Log udp_sendto return value and ref count (first 10 times) */
    static uint32_t sendto_motor_log_count = 0;
    if (sendto_motor_log_count < 10) {
        DebugP_log("[xGW UDP] Motor: udp_sendto err=%d, p->ref=%u (before free), sendto_cnt=%u\r\n",
                   err, p->ref, g_udp_sendto_count);
        sendto_motor_log_count++;
    }

    /* [DEBUG] Log pbuf ref count before free (first 5 times) */
    static uint32_t pbuf_motor_dbg_count = 0;
    if (pbuf_motor_dbg_count < 5) {
        DebugP_log("[xGW UDP] Motor: pbuf_free called, ref=%u, p=%p, alloc_cnt=%u, free_cnt=%u\r\n",
                   p->ref, p, g_pbuf_alloc_count, g_pbuf_free_count);
        pbuf_motor_dbg_count++;
    }

    /* Free pbuf */
    pbuf_free(p);
    g_pbuf_free_count++;

    /* [QA TRACE T020] GPIO PA4 LOW on UDP TX exit */
    /* TODO: Enable GPIO instrumentation pins in SysConfig before uncommenting
    GPIO_pinWriteLow(baseAddr, DEBUG_GPIO_UDP_TX_PIN);
    */

    /* Debug: Log error details (first 3 times only) */
    static uint32_t send_err_log_count = 0;
    if (err != ERR_OK) {
        g_udp_state.tx_errors++;
        if (send_err_log_count < 3) {
            DebugP_log("[xGW UDP] Motor: udp_sendto FAILED! err=%d, len=%u\r\n", err, total_len);
            send_err_log_count++;
        }
        return -1;
    }

    g_udp_state.tx_count++;
    return total_len;
}

int xgw_udp_send_imu_state(const xgw_imu_state_t* imu_state)
{
    if (!g_udp_state.started || imu_state == NULL) {
        return -1;
    }

    /* [TEST] Increase IMU packet size from 100 to 1024 bytes to test pbuf exhaustion */
    uint16_t payload_len = 900;  /* Instead of sizeof(xgw_imu_state_t) = 68 */
    uint16_t total_len = sizeof(xgw_header_t) + payload_len;
    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, total_len, PBUF_RAM);

    if (p == NULL) {
        g_pbuf_alloc_fail_count++;
        DebugP_log("[xGW UDP] IMU: pbuf_alloc FAILED! size=%u, alloc_cnt=%u, free_cnt=%u, fail_cnt=%u\r\n",
                   total_len, g_pbuf_alloc_count, g_pbuf_free_count, g_pbuf_alloc_fail_count);
        g_udp_state.tx_errors++;
        return -1;
    }

    g_pbuf_alloc_count++;

    /* Build packet */
    uint8_t* data = (uint8_t*)p->payload;

    /* Initialize header */
    xgw_header_t* header = (xgw_header_t*)data;
    xgw_header_init(header, XGW_MSG_TYPE_IMU_STATE, 1, payload_len, g_udp_state.sequence++);

    /* Set timestamp */
    header->timestamp_ns = ClockP_getTimeUsec() * 1000ULL;

    /* Copy IMU state (first 68 bytes) */
    memcpy(data + sizeof(xgw_header_t), imu_state, sizeof(xgw_imu_state_t));

    /* [TEST] Fill remaining payload with test pattern (0xAA) */
    if (payload_len > sizeof(xgw_imu_state_t)) {
        memset(data + sizeof(xgw_header_t) + sizeof(xgw_imu_state_t),
               0xAA, payload_len - sizeof(xgw_imu_state_t));
    }

    /* Calculate CRC */
    header->crc32 = xgw_crc32_calculate(header, data + sizeof(xgw_header_t), payload_len);

    /* Send packet - MUST lock tcpip core when calling from non-tcpip thread */
    LOCK_TCPIP_CORE();
    err_t err = udp_sendto(g_udp_tx_pcb, p, &g_pc_ip_addr, XGW_UDP_TX_PORT);
    UNLOCK_TCPIP_CORE();

    g_udp_sendto_count++;

    /* [DEBUG] Log IMU udp_sendto (first 5 times) */
    static uint32_t imu_sendto_log_count = 0;
    if (imu_sendto_log_count < 5) {
        DebugP_log("[xGW UDP] IMU: udp_sendto err=%d, p->ref=%u, alloc_cnt=%u\r\n",
                   err, p->ref, g_pbuf_alloc_count);
        imu_sendto_log_count++;
    }

    /* Free pbuf */
    pbuf_free(p);
    g_pbuf_free_count++;

    /* Debug: Log IMU send errors (first 3 times) */
    static uint32_t imu_err_log_count = 0;
    if (err != ERR_OK) {
        g_udp_state.tx_errors++;
        if (imu_err_log_count < 3) {
            DebugP_log("[xGW UDP] IMU: udp_sendto FAILED! err=%d, len=%u\r\n", err, total_len);
            imu_err_log_count++;
        }
        return -1;
    }

    g_udp_state.tx_count++;
    return total_len;
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

int xgw_udp_send_raw(const uint8_t* data, uint16_t len)
{
    if (!g_udp_state.started || data == NULL || len == 0) {
        return -1;
    }

    if (g_udp_tx_pcb == NULL) {
        return -1;
    }

    /* Limit max size */
    if (len > XGW_UDP_MAX_PACKET_SIZE) {
        len = XGW_UDP_MAX_PACKET_SIZE;
    }

    /* Allocate pbuf for packet */
    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);

    if (p == NULL) {
        g_udp_state.tx_errors++;
        return -1;
    }

    /* Copy data */
    memcpy(p->payload, data, len);

    /* Send packet */
    LOCK_TCPIP_CORE();
    err_t err = udp_sendto(g_udp_tx_pcb, p, &g_pc_ip_addr, XGW_UDP_TX_PORT);
    UNLOCK_TCPIP_CORE();

    /* Free pbuf */
    pbuf_free(p);

    if (err != ERR_OK) {
        g_udp_state.tx_errors++;
        return -1;
    }

    g_udp_state.tx_count++;
    return len;
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
    uint32_t bytes_to_write = count * sizeof(motor_cmd_ipc_t);

    DebugP_log("[xGW UDP] Sending %d cmds, %u bytes to IPC...\r\n", count, bytes_to_write);
    int ret = gateway_ringbuf_core0_send(ipc_cmds, bytes_to_write, &bytes_written);

    if (ret == 0) {
        /* Notify Core 1 */
        DebugP_log("[xGW UDP] IPC send OK: %u bytes written\r\n", bytes_written);
        gateway_notify_commands_ready();
        g_udp_state.rx_count++;
        return 0;
    } else {
        DebugP_log("[xGW UDP] ERROR: Failed to write motor commands! ret=%d, written=%u/%u\r\n",
                   ret, bytes_written, bytes_to_write);
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

    /* Convert ports from network byte order to host byte order */
    uint16_t src_port = lwip_ntohs(port);  /* Source port (PC) */
    uint16_t dst_port = lwip_ntohs(pcb->local_port);  /* Destination port (MCU) */

    /* [DEBUG-UDP-RX] Check if this is RX or TX PCB */
    int is_rx_pcb = (pcb == g_udp_rx_pcb);
    int is_tx_pcb = (pcb == g_udp_tx_pcb);

    /* [DEBUG-UDP-RX] Entry point - always log this FIRST */
    DebugP_log("[xGW UDP] === RX ENTRY === pcb=%p (rx=%d,tx=%d), src_port=%d, dst_port=%d\r\n",
               pcb, is_rx_pcb, is_tx_pcb, src_port, dst_port);

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

        /* lwIP already filtered by port via udp_bind(), no need to check port here */
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
        DebugP_log("[xGW UDP] RX: Invalid length: %d\r\n", p ? p->len : 0);
    }

    /* Free pbuf */
    pbuf_free(p);

    /* [QA TRACE T019] GPIO PA5 LOW on UDP RX exit */
    /* TODO: Enable GPIO instrumentation pins in SysConfig before uncommenting
    GPIO_pinWriteLow(baseAddr, DEBUG_GPIO_UDP_RX_PIN);
    */
}
