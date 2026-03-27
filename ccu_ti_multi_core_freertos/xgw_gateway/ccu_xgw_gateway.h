/**
 * @file ccu_xgw_gateway.h
 * @brief ccu_ti xGW Gateway integration layer for Core0 (FreeRTOS)
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h]
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

#ifndef CCU_XGW_GATEWAY_H_
#define CCU_XGW_GATEWAY_H_

/* IMU enabled - data comes from Core1 via IPC */
#define CCU_IMU_ENABLED 1

/* Includes */
#include "../motor_mapping.h"
#include "../common/xgw_protocol.h"
#include "../../gateway_shared.h"
#include <stdint.h>
#include <stdbool.h>

/* lwIP includes for UDP/IP */
#include "lwip/api.h"
#include "lwip/udp.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * FREERTOS INCLUDES
 *============================================================================*/

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

/* Task priorities (higher number = higher priority in FreeRTOS)
 * Main task uses configMAX_PRIORITIES-1, but deletes itself after starting gateway
 */
#define XGW_TASK_PRIORITY_UDP_RX        (configMAX_PRIORITIES - 3)
#define XGW_TASK_PRIORITY_UDP_TX        (configMAX_PRIORITIES - 3)

/* Task stack sizes (in words)
 * CRASH FIX: Increased from 8KB to 32KB after stack overflow at 175s
 */
#define XGW_TASK_STACK_SIZE_UDP_RX      4096  /* 32KB */
#define XGW_TASK_STACK_SIZE_UDP_TX      4096  /* 32KB */

/* Queue sizes */
#define XGW_UDP_RX_QUEUE_SIZE           50   /* 50 * 2048 = 100KB */

/* Timing */
#define XGW_UDP_TX_PERIOD_MS            2   /* 500Hz - 2ms period */

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

/**
 * @brief UDP RX queue item
 */
typedef struct {
    uint8_t     data[2048]; /* UDP received data */
    uint16_t    length;     /* Data length */
    ip_addr_t   src_addr;   /* Source IP address */
    uint16_t    src_port;   /* Source port */
} xgw_udp_rx_item_t;

/**
 * @brief Gateway configuration structure
 */
typedef struct {
    /* Network configuration */
    uint16_t    udp_rx_port;       /* UDP RX port (default: 61904) */
    uint16_t    udp_tx_port;       /* UDP TX port (default: 53489) */
    ip_addr_t   pc_ip_addr;        /* PC IP address for TX */

    /* Sequence counters */
    uint32_t    tx_sequence;       /* TX sequence number */
    uint32_t    rx_sequence;       /* RX sequence number */
} xgw_gateway_config_t;

/**
 * @brief Gateway status structure
 */
typedef struct {
    bool    initialized;        /* Initialization status */
    bool    running;            /* Running status */
    uint32_t udp_rx_count;      /* UDP RX packet count */
    uint32_t udp_tx_count;      /* UDP TX packet count */
    uint32_t parse_errors;      /* Parse error count */
    uint32_t crc_errors;        /* CRC error count */

    /* IPC statistics */
    uint32_t motor_state_updates; /* Motor states received from Core1 */
    uint32_t imu_state_updates;   /* IMU states received from Core1 */
} xgw_gateway_status_t;

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * @brief Initialize XGW Gateway
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:202]
 *
 * @return 0 on success, -1 on error
 */
int xgw_gateway_init(void);

/**
 * @brief Start XGW Gateway
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:211]
 *
 * @return 0 on success, -1 on error
 */
int xgw_gateway_start(void);

/**
 * @brief Stop XGW Gateway
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:218]
 */
void xgw_gateway_stop(void);

/**
 * @brief Get gateway status
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:225]
 *
 * @param status Output status structure
 */
void xgw_gateway_get_status(xgw_gateway_status_t* status);

/**
 * @brief Initialize lwIP UDP PCBs (must be called AFTER tcpip_init)
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:235]
 *
 * @return 0 on success, -1 on error
 */
int xgw_gateway_lwip_init(void);

/*==============================================================================
 * CALLBACK FUNCTIONS
 *============================================================================*/

/**
 * @brief UDP RX callback (called from lwIP receive function)
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:277]
 *
 * @param data Received UDP data
 * @param length Data length
 * @param src_addr Source IP address
 * @param src_port Source port
 */
void xgw_udp_rx_callback(const uint8_t* data, uint16_t length,
                         const ip_addr_t* src_addr, uint16_t src_port);

/*==============================================================================
 * PROTOCOL HANDLING FUNCTIONS
 *============================================================================*/

/**
 * @brief Process Motor Command packet (Type 0x02)
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:292]
 * [MODIFIED: Sends motor commands via IPC to Core1 instead of CAN TX queue]
 *
 * @param header Packet header
 * @param payload Payload data
 * @param payload_len Payload length
 * @return 0 on success, -1 on error
 */
int xgw_process_motor_cmd(const xgw_header_t* header, const void* payload, uint32_t payload_len);

/**
 * @brief Process Motor Set packet (Type 0x07)
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:302]
 * [MODIFIED: Sends motor set commands via IPC to Core1]
 *
 * @param header Packet header
 * @param payload Payload data
 * @param payload_len Payload length
 * @return 0 on success, -1 on error
 */
int xgw_process_motor_set(const xgw_header_t* header, const void* payload, uint32_t payload_len);

/**
 * @brief Process Configuration packet (Type 0x06)
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:312]
 *
 * @param header Packet header
 * @param payload Payload data
 * @param payload_len Payload length
 * @return 0 on success, -1 on error
 */
int xgw_process_config(const xgw_header_t* header, const void* payload, uint32_t payload_len);

/**
 * @brief Build Motor State packet for UDP TX
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:321]
 * [MODIFIED: Reads motor states from IPC (Core1) instead of shared buffer]
 *
 * @param buffer Output buffer
 * @param buffer_len Buffer length
 * @return Packet size on success, -1 on error
 */
int xgw_build_motor_state_packet(uint8_t* buffer, uint32_t buffer_len);

/**
 * @brief Build IMU State packet for UDP TX
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:331]
 * [MODIFIED: Reads IMU state from IPC (Core1) instead of shared buffer]
 *
 * @param buffer Output buffer
 * @param buffer_len Buffer length
 * @return Packet size on success, -1 on error
 */
int xgw_build_imu_state_packet(uint8_t* buffer, uint32_t buffer_len);

/**
 * @brief Build Diagnostics packet for UDP TX
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:341]
 *
 * @param buffer Output buffer
 * @param buffer_len Buffer length
 * @return Packet size on success, -1 on error
 */
int xgw_build_diag_packet(uint8_t* buffer, uint32_t buffer_len);

/*==============================================================================
 * DEBUG/LOG FUNCTIONS
 *============================================================================*/

/**
 * @brief Debug log function
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:382]
 */
void xgw_debug_log(const char* format, ...);

/**
 * @brief Error log function
 *
 * [MIGRATED FROM draft/ccu_ti/ccu_xgw_gateway.h:390]
 */
void xgw_error_log(const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif /* CCU_XGW_GATEWAY_H_ */
