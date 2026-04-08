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

/* [FIX B070] UDP RX Queue and Task
 * Queue-based processing prevents lwIP thread blocking
 * Reference: ccu_ti/ccu_xgw_gateway.c */
static QueueHandle_t g_udp_rx_queue = NULL;
static TaskHandle_t g_udp_rx_task_handle = NULL;
static volatile bool g_udp_rx_task_running = false;

/* [DEBUG B071] Debug counters for UDP RX queue processing */
volatile uint32_t g_udp_rx_callback_count = 0;      /* Packets received in callback */
volatile uint32_t g_udp_rx_queue_success = 0;       /* Successfully queued */
volatile uint32_t g_udp_rx_queue_full = 0;          /* Queue full errors */
volatile uint32_t g_udp_rx_task_wakeups = 0;        /* Task notified */
volatile uint32_t g_udp_rx_task_packets = 0;        /* Packets processed by task */

/*==============================================================================
 * FORWARD DECLARATIONS
 *============================================================================*/

static void xgw_udp_recv_callback(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                                   const ip_addr_t* addr, u16_t port);

/* [FIX B070] UDP RX Task */
static void udp_rx_task(void* parameters);

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

    /* [FIX B070] Create UDP RX queue */
    g_udp_rx_queue = xQueueCreate(XGW_UDP_RX_QUEUE_SIZE, sizeof(xgw_udp_rx_item_t));
    if (g_udp_rx_queue == NULL) {
        DebugP_log("[xGW UDP] ERROR: Failed to create RX queue!\r\n");
        return -1;
    }
    DebugP_log("[xGW UDP] RX queue created: size=%d\r\n", XGW_UDP_RX_QUEUE_SIZE);

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

    /* [DEBUG B066] Log packet details - first 3 times */
    static uint32_t packet_log_count = 0;
    if (packet_log_count < 3) {
        DebugP_log("[xGW UDP] Motor: SENT count=%u, payload_len=%u, total=%u, motors[0].id=%u, motors[0].pos=%.3f\r\n",
                   count, payload_len, total_len,
                   states[0].motor_id, states[0].position);
        packet_log_count++;
    }

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
        /* [FIX B089] Preserve mode from UDP packet instead of overriding to 255
         * Problem: PC sends enable command (mode=1) but code overrides to mode=255 (MIT)
         * Core1 then sends MIT frame instead of Enable frame → motor not enabled!
         * Solution: Keep mode from packet so Core1 can distinguish:
         * - mode 0-4: One-time commands (disable/enable/zero) → send Enable/Disable frames
         * - mode 255 or unset: MIT motion control → send MIT frames
         * Reference: draft/ccu_ti/ccu_xgw_gateway.c doesn't use mode field, but PC may send it */
        ipc_cmds[i].mode = cmds[i].mode;  /* Preserve mode from packet */
        /* [FIX B038] Use float directly in IPC (no more ×100 scaling)
         * Matches reference architecture: float directly from UDP to CAN */
        ipc_cmds[i].position = cmds[i].position;  /* rad (float) */
        ipc_cmds[i].velocity = cmds[i].velocity;  /* rad/s (float) */
        ipc_cmds[i].torque = cmds[i].torque;      /* Nm (float) */
        ipc_cmds[i].kp = cmds[i].kp;              /* Kp (float) */
        ipc_cmds[i].kd = cmds[i].kd;              /* Kd (float) */
    }

    /* Write to shared memory via ring buffer */
    uint32_t bytes_written = 0;
    uint32_t bytes_to_write = count * sizeof(motor_cmd_ipc_t);

    int ret = gateway_ringbuf_core0_send(ipc_cmds, bytes_to_write, &bytes_written);

    if (ret == 0) {
        /* Notify Core 1 */
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
    /* [DEBUG B071] Static counter to log first few motor_set packets */
    static uint32_t motor_set_count = 0;
    motor_set_count++;

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
        return 0;
    } else {
        /* [DEBUG B097] Log ring buffer write failure */
        DebugP_log("[xGW UDP] ERROR: Motor SET #%u: ringbuf write failed! ret=%d, count=%u, size=%u\r\n",
                   motor_set_count, ret, count, count * (uint32_t)sizeof(motor_cmd_ipc_t));
        g_udp_state.rx_errors++;
        return -1;
    }
}

/*==============================================================================
 * [FIX B070] UDP RX TASK - Queue-based packet processing
 *============================================================================
 *
 * Processes UDP packets from the RX queue in task context (not lwIP thread).
 * This prevents blocking in the lwIP tcpip_thread when:
 * - Ring buffer is full
 * - DebugP_log() is called (UART blocking)
 * - IPC operations take time
 *
 * Reference: ccu_ti/ccu_xgw_gateway.c:udp_rx_task()
 */

/**
 * @brief UDP RX task - Process packets from queue
 *
 * Waits for notification from callback, then processes all queued packets.
 */
static void udp_rx_task(void* parameters)
{
    (void)parameters;
    xgw_udp_rx_item_t rx_item;
    uint32_t packets_processed = 0;
    uint32_t last_stats_time = xTaskGetTickCount();
    uint32_t last_log_time = xTaskGetTickCount();

    while (g_udp_rx_task_running) {
        /* Wait for notification from callback (timeout 1ms for stats update) */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));

        /* Process all queued packets */
        while (xQueueReceive(g_udp_rx_queue, &rx_item, 0) == pdTRUE) {
            packets_processed++;
            g_udp_rx_task_packets++;

            /* Check minimum packet size (header only) */
            if (rx_item.length < sizeof(xgw_header_t)) {
                g_udp_state.parse_errors++;
                continue;
            }

            /* Parse header */
            const xgw_header_t* header = (const xgw_header_t*)rx_item.data;

            /* Validate magic */
            if (header->magic != XGW_PROTOCOL_MAGIC) {
                g_udp_state.parse_errors++;
                continue;
            }

            /* Process by message type */
            switch (header->msg_type) {
                case XGW_MSG_TYPE_MOTOR_CMD:
                    if (xgw_udp_process_motor_cmd(rx_item.data, rx_item.length) == 0) {
                        g_udp_state.rx_count++;
                    } else {
                        g_udp_state.parse_errors++;
                    }
                    break;

                case XGW_MSG_TYPE_MOTOR_SET:
                    if (xgw_udp_process_motor_set(rx_item.data, rx_item.length) == 0) {
                        g_udp_state.rx_count++;
                    } else {
                        g_udp_state.parse_errors++;
                    }
                    break;

                default:
                    g_udp_state.parse_errors++;
                    break;
            }
        }

        /* Periodic stats logging (every 5 seconds) */
        uint32_t current_time = xTaskGetTickCount();
        if (current_time - last_stats_time >= pdMS_TO_TICKS(5000)) {
            if (packets_processed > 0) {
                DebugP_log("[xGW UDP] RX task: processed %u packets in 5s (total=%u, callback=%u, queued=%u, full=%u)\r\n",
                           packets_processed, g_udp_rx_task_packets,
                           g_udp_rx_callback_count, g_udp_rx_queue_success, g_udp_rx_queue_full);
                packets_processed = 0;
            }
            last_stats_time = current_time;
        }

        /* [DEBUG B071] Initial status log (first 10 seconds) */
        if (current_time - last_log_time >= pdMS_TO_TICKS(10000) && g_udp_rx_task_packets < 100) {
            DebugP_log("[xGW UDP] Status: callback=%u, queued=%u, task_packets=%u, queue_full=%u\r\n",
                       g_udp_rx_callback_count, g_udp_rx_queue_success, g_udp_rx_task_packets, g_udp_rx_queue_full);
            last_log_time = current_time;
        }
    }

    vTaskDelete(NULL);
}

/*==============================================================================
 * [FIX B070] UDP RX TASK START/STOP FUNCTIONS
 *============================================================================*/

/**
 * @brief Start UDP RX task
 *
 * Creates the FreeRTOS task that processes packets from the RX queue.
 * Must be called after xgw_udp_start().
 *
 * @param task_stack Stack buffer for the task
 * @param task_stack_size Size of stack buffer in bytes
 * @param task_tcb Task control block buffer (StaticTask_t) for xTaskCreateStatic
 * @return 0 on success, -1 on error
 */
int xgw_udp_start_rx_task(void* task_stack, uint32_t task_stack_size, void* task_tcb)
{
    if (g_udp_rx_task_running) {
        DebugP_log("[xGW UDP] RX task already running\r\n");
        return 0;
    }

    if (g_udp_rx_queue == NULL) {
        DebugP_log("[xGW UDP] ERROR: RX queue not created!\r\n");
        return -1;
    }

    if (task_tcb == NULL) {
        DebugP_log("[xGW UDP] ERROR: Task TCB buffer is NULL!\r\n");
        return -1;
    }

    /* [DEBUG B071] Debug logging */
    DebugP_log("[xGW UDP] Starting RX task: stack_size=%u, tcb=%p, priority=%u\r\n",
               task_stack_size, task_tcb, XGW_UDP_RX_TASK_PRIORITY);

    /* Create task with static stack and TCB */
    g_udp_rx_task_running = true;
    g_udp_rx_task_handle = xTaskCreateStatic(
        udp_rx_task,
        "udp_rx_task",
        task_stack_size / sizeof(configSTACK_DEPTH_TYPE),
        NULL,
        XGW_UDP_RX_TASK_PRIORITY,
        (StackType_t*)task_stack,
        (StaticTask_t*)task_tcb  /* Task control block from caller */
    );

    if (g_udp_rx_task_handle == NULL) {
        DebugP_log("[xGW UDP] ERROR: Failed to create RX task!\r\n");
        g_udp_rx_task_running = false;
        return -1;
    }

    DebugP_log("[xGW UDP] RX task created successfully, handle=%p\r\n", g_udp_rx_task_handle);

    /* Give task time to start and log its startup message */
    vTaskDelay(pdMS_TO_TICKS(10));

    return 0;
}

/**
 * @brief Stop UDP RX task
 *
 * Gracefully stops the UDP RX task.
 */
void xgw_udp_stop_rx_task(void)
{
    if (g_udp_rx_task_running) {
        g_udp_rx_task_running = false;

        /* Notify task to wake it up */
        if (g_udp_rx_task_handle != NULL) {
            vTaskNotifyGiveFromISR(g_udp_rx_task_handle, NULL);
            g_udp_rx_task_handle = NULL;
        }

        DebugP_log("[xGW UDP] RX task stop requested\r\n");
    }
}

/*==============================================================================
 * UDP RX CALLBACK
 *============================================================================*/

/**
 * @brief UDP RX receive callback (lwIP tcpip_thread context)
 *
 * [FIX B070] ONLY queue packets for processing - NO blocking operations!
 * Reference: ccu_ti/ccu_xgw_gateway.c:udp_recv_callback()
 *
 * CRITICAL: This runs in lwIP tcpip_thread context. Must NOT block!
 * - NO DebugP_log() (blocks on UART)
 * - NO ringbuf operations (can block if full)
 * - NO IPC notifications (can block)
 * Only copy packet to queue and return immediately.
 */
static void xgw_udp_recv_callback(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                                   const ip_addr_t* addr, u16_t port)
{
    (void)arg;
    (void)pcb;  /* PCB already validated by lwIP via udp_bind() */

    /* [DEBUG B071] Count every callback invocation */
    g_udp_rx_callback_count++;

    /* [FIX B070] Queue-based processing - copy packet to queue and return */
    if (p != NULL && g_udp_rx_queue != NULL) {
        xgw_udp_rx_item_t rx_item;
        /* [FIX B080] Use tot_len for pbuf chains - p->len is only first pbuf! */
        rx_item.length = (p->tot_len > sizeof(rx_item.data)) ? sizeof(rx_item.data) : p->tot_len;

        /* Copy packet data to queue item */
        pbuf_copy_partial(p, rx_item.data, rx_item.length, 0);

        /* Copy source address and port */
        ip_addr_copy(rx_item.src_addr, *addr);
        rx_item.src_port = port;

        /* Try to queue packet - non-blocking! */
        BaseType_t queue_result = xQueueSend(g_udp_rx_queue, &rx_item, 0);

        if (queue_result == pdTRUE) {
            /* Successfully queued - notify UDP RX task */
            g_udp_rx_queue_success++;
            if (g_udp_rx_task_handle != NULL) {
                BaseType_t higher_priority_task_woken = pdFALSE;
                vTaskNotifyGiveFromISR(g_udp_rx_task_handle, &higher_priority_task_woken);
                portYIELD_FROM_ISR(higher_priority_task_woken);
                g_udp_rx_task_wakeups++;
            }
        } else {
            /* Queue full - packet dropped, update error counter */
            g_udp_rx_queue_full++;
            g_udp_state.rx_errors++;
        }
    }

    /* Always free pbuf - MUST be done in callback! */
    if (p != NULL) {
        pbuf_free(p);
    }
}
