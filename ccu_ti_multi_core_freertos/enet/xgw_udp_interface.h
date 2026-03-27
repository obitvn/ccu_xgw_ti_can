/**
 * @file xgw_udp_interface.h
 * @brief xGW UDP Interface for Core 0 (FreeRTOS)
 *
 * Simplified UDP interface for xGW protocol communication
 * Handles UDP TX/RX for motor commands and states
 *
 * @author CCU Multicore Project
 * @date 2026-03-24
 */

#ifndef XGW_UDP_INTERFACE_H_
#define XGW_UDP_INTERFACE_H_

#include <stdint.h>
#include <stdbool.h>
#include "../common/xgw_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UDP Port Configuration */
#define XGW_UDP_RX_PORT        61904    /* PC sends to xGW */
#define XGW_UDP_TX_PORT        53489    /* xGW sends to PC */
#define XGW_UDP_IP_ADDR_DEFAULT    0   /* 0.0.0.0 - will be configured later */
#define XGW_UDP_MAX_PACKET_SIZE     1500

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

#ifdef __cplusplus
}
#endif

#endif /* XGW_UDP_INTERFACE_H_ */
