/**
 * @file xgw_udp_interface.h
 * @brief xGW UDP Interface for Core 0 (FreeRTOS)
 *
 * Simplified UDP interface for xGW protocol communication
 * Handles UDP TX/RX for motor commands and states
 *
 * @author CCU Multicore Project
 * @date 2026-03-24
 * @date 2026-04-07 - Added UDP RX queue for non-blocking processing (FIX B070)
 */

#ifndef XGW_UDP_INTERFACE_H_
#define XGW_UDP_INTERFACE_H_

#include <stdint.h>
#include <stdbool.h>
#include "../common/xgw_protocol.h"
#include "lwip/ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UDP Port Configuration */
#define XGW_UDP_RX_PORT            61904    /* PC sends to xGW */
#define XGW_UDP_TX_PORT            53489    /* xGW sends to PC */
#define XGW_UDP_IP_ADDR_DEFAULT    0        /* 0.0.0.0 - will be configured later */
#define XGW_UDP_MAX_PACKET_SIZE    1500

/* [FIX B070] UDP RX Queue Configuration
 * Queue-based processing prevents lwIP thread blocking
 * Reference: ccu_ti/ccu_xgw_gateway.c
 */
#define XGW_UDP_RX_QUEUE_SIZE      50       /* Queue depth - 50 packets */
#define XGW_UDP_RX_TASK_STACK_SIZE 4096     /* Task stack size - [FIX B073] Increased from 2048 to prevent overflow */
#define XGW_UDP_RX_TASK_PRIORITY   (configMAX_PRIORITIES - 3)  /* High priority */

/*==============================================================================
 * [FIX B070] UDP RX QUEUE - Non-blocking packet processing
 *============================================================================
 *
 * Problem: Processing packets directly in lwIP tcpip_thread callback causes
 *          blocking when ring buffer is full (DebugP_log on UART).
 *
 * Solution: Queue packets in callback, process in separate FreeRTOS task.
 * Reference: ccu_ti/ccu_xgw_gateway.c
 */

/**
 * @brief UDP RX queue item
 *
 * Holds a received UDP packet for processing in task context
 */
typedef struct {
    uint8_t     data[XGW_UDP_MAX_PACKET_SIZE];  /* Packet data */
    uint16_t    length;                          /* Data length */
    ip_addr_t   src_addr;                        /* Source IP address */
    u16_t       src_port;                        /* Source port */
} xgw_udp_rx_item_t;

/* Forward declarations for FreeRTOS types (avoid including FreeRTOS headers) */
struct QueueDefinition;
typedef struct QueueDefinition* QueueHandle_t;

/* UDP Interface State */
typedef struct {
    bool initialized;
    bool started;             /* UDP PCBs created and bound */
    uint32_t rx_count;
    uint32_t tx_count;
    uint32_t rx_errors;
    uint32_t tx_errors;
    uint32_t parse_errors;
    uint32_t crc_errors;
    uint32_t sequence;          /* TX sequence number */
} xgw_udp_state_t;

/* UDP RX Callback Type */
typedef void (*xgw_udp_rx_callback_t)(const uint8_t* data, uint16_t length,
                                      const uint8_t* src_addr, uint16_t src_port);

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * @brief Initialize xGW UDP interface
 *
 * @return 0 on success, -1 on error
 */
int xgw_udp_init(void);

/**
 * @brief Initialize UDP PCBs and start listening
 *
 * Must be called after tcpip_init() completes
 *
 * @return 0 on success, -1 on error
 */
int xgw_udp_start(void);

/**
 * @brief Send motor states via UDP
 *
 * @param states Motor states array
 * @param count Number of motors
 * @return Number of bytes sent, or -1 on error
 */
int xgw_udp_send_motor_states(const xgw_motor_state_t* states, uint8_t count);

/**
 * @brief Send IMU state via UDP
 *
 * @param imu_state IMU state data
 * @return Number of bytes sent, or -1 on error
 */
int xgw_udp_send_imu_state(const xgw_imu_state_t* imu_state);

/**
 * @brief Send diagnostics via UDP
 *
 * @param diag Diagnostics data
 * @return Number of bytes sent, or -1 on error
 */
int xgw_udp_send_diagnostics(const xgw_diag_t* diag);

/**
 * @brief Register UDP RX callback
 *
 * Called when UDP packet is received
 *
 * @param callback Callback function
 * @return 0 on success, -1 on error
 */
int xgw_udp_register_rx_callback(xgw_udp_rx_callback_t callback);

/**
 * @brief Get UDP interface state
 *
 * @param state Output state structure
 */
void xgw_udp_get_state(xgw_udp_state_t* state);

/**
 * @brief Check if UDP interface is initialized
 *
 * @return true if initialized
 */
bool xgw_udp_is_initialized(void);

/**
 * @brief Set PC IP address for UDP TX
 *
 * @param ip_addr Pointer to IP address (4 bytes for IPv4)
 * @return 0 on success, -1 on error
 */
int xgw_udp_set_pc_ip(const uint8_t* ip_addr);

/**
 * @brief Get current PC IP address
 *
 * @param ip_addr Pointer to store IP address (4 bytes)
 * @return 0 on success, -1 on error
 */
int xgw_udp_get_pc_ip(uint8_t* ip_addr);

/**
 * @brief Send raw UDP packet (for testing/debugging)
 *
 * @param data Raw data to send
 * @param len Length of data
 * @return bytes sent on success, -1 on error
 */
int xgw_udp_send_raw(const uint8_t* data, uint16_t len);

/**
 * @brief Process received motor command packet
 *
 * @param data Packet data
 * @param length Packet length
 * @return 0 on success, -1 on error
 */
int xgw_udp_process_motor_cmd(const uint8_t* data, uint16_t length);

/**
 * @brief Process received motor set packet
 *
 * @param data Packet data
 * @param length Packet length
 * @return 0 on success, -1 on error
 */
int xgw_udp_process_motor_set(const uint8_t* data, uint16_t length);

/*==============================================================================
 * [FIX B070] UDP RX TASK API
 *============================================================================
 *
 * Queue-based UDP RX processing to prevent lwIP thread blocking
 */

/**
 * @brief Start UDP RX task
 *
 * Creates the FreeRTOS task that processes packets from the RX queue.
 * Must be called after xgw_udp_start().
 *
 * @param task_stack Stack buffer for the task (must be XGW_UDP_RX_TASK_STACK_SIZE)
 * @param task_stack_size Size of stack buffer in bytes
 * @param task_tcb Task control block buffer (StaticTask_t) for xTaskCreateStatic
 * @return 0 on success, -1 on error
 */
int xgw_udp_start_rx_task(void* task_stack, uint32_t task_stack_size, void* task_tcb);

/**
 * @brief Stop UDP RX task
 *
 * Gracefully stops the UDP RX task.
 */
void xgw_udp_stop_rx_task(void);

/*==============================================================================
 * DEBUG STATISTICS (visible for main.c)
 *============================================================================*/

/* [DEBUG B029] Pbuf tracking counters - exported for stats display */
extern volatile uint32_t g_pbuf_alloc_count;
extern volatile uint32_t g_pbuf_free_count;
extern volatile uint32_t g_pbuf_alloc_fail_count;
extern volatile uint32_t g_udp_sendto_count;

/* [DEBUG B071] UDP RX queue debug counters */
extern volatile uint32_t g_udp_rx_callback_count;   /* Packets received in callback */
extern volatile uint32_t g_udp_rx_queue_success;    /* Successfully queued */
extern volatile uint32_t g_udp_rx_queue_full;       /* Queue full errors */
extern volatile uint32_t g_udp_rx_task_wakeups;     /* Task notified */
extern volatile uint32_t g_udp_rx_task_packets;     /* Packets processed by task */

#ifdef __cplusplus
}
#endif

#endif /* XGW_UDP_INTERFACE_H_ */
