/**
 * @file yis320_protocol.h
 * @brief YIS320 IMU protocol implementation for CCU Multicore Gateway (Core 1 - Bare Metal)
 *
 * Migrated from draft/ccu_ti/imu/yis320/yis320_protocol.h
 * - Removed FreeRTOS dependencies
 * - Added IPC support for gateway_shared.h
 * - Bare metal critical sections
 *
 * @author Migrated from draft/ccu_ti by Chu Tien Thinh
 * @date 2026-03-27
 */

#ifndef CCU_TI_YIS320_PROTOCOL_H_
#define CCU_TI_YIS320_PROTOCOL_H_

#include "../imu_protocol_handler.h"
#include "../../motor_mapping.h"
#include "../../../gateway_shared.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define YIS320_HEADER_BYTE_1     0x59
#define YIS320_HEADER_BYTE_2     0x53
#define YIS320_BUFFER_SIZE       1024
#define YIS320_DEFAULT_BAUD_RATE 921600

// Degrees to radians conversion
// [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:34]
#define YIS320_DEG_TO_RAD        (3.14159265358979323846f / 180.0f)
// Data factor for NOT_MAG_DATA
// [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:36]
#define YIS320_DATA_FACTOR       0.000001f

// YIS320 Data IDs
// [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:38-52]
#define YIS320_DATA_ID_TEMPERATURE       0x01
#define YIS320_DATA_ID_ACCELEROMETER     0x10
#define YIS320_DATA_ID_GYROSCOPE         0x20
#define YIS320_DATA_ID_MAGNETIC_NORM     0x30
#define YIS320_DATA_ID_MAGNETIC_RAW      0x31
#define YIS320_DATA_ID_EULER_ANGLES      0x40
#define YIS320_DATA_ID_QUATERNION        0x41
#define YIS320_DATA_ID_UTC_TIME          0x50
#define YIS320_DATA_ID_SAMPLE_TIMESTAMP  0x51
#define YIS320_DATA_ID_DR_TIMESTAMP      0x52
#define YIS320_DATA_ID_LOCATION          0x60
#define YIS320_DATA_ID_LOCATION_HP       0x68
#define YIS320_DATA_ID_VELOCITY          0x70
#define YIS320_DATA_ID_NAV_STATUS        0x80

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

/**
 * @brief YIS320 protocol handler private data
 *
 * OPTIMIZATION: Linear buffer with deferred memmove
 * - read_pos: Current buffer fill position (acts as write pointer)
 * - Data is appended at read_pos
 * - Parser tracks consumed bytes separately
 * - Single memmove at END of parsing (not after each frame)
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:67-73]
 */
typedef struct {
    uint8_t  rx_buffer[YIS320_BUFFER_SIZE];  // RX buffer for incoming data
    volatile uint16_t read_pos;              // Buffer position (current fill level)
    bool     is_connected;                   // Connection status
    uint8_t  uart_port;                      // UART port number (platform specific)
    uint32_t baud_rate;                      // Baud rate
} yis320_private_data_t;

/**
 * @brief Static instance for YIS320 private data
 *
 * Using static allocation instead of dynamic malloc to avoid:
 * - Memory fragmentation
 * - Out-of-memory issues
 * - Memory leaks
 *
 * Only one IMU is supported at a time with this approach.
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:85]
 */
extern yis320_private_data_t g_yis320_private_data;

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * @brief Initialize YIS320 protocol handler
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:97]
 *
 * @param handler IMU protocol handler instance to initialize
 * @return 0 on success, -1 on error
 */
int yis320_protocol_handler_init(imu_protocol_handler_t* handler);

/**
 * @brief Initialize YIS320 IMU hardware connection
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:105]
 *
 * @param handler IMU protocol handler instance
 * @return 0 on success, -1 on error
 */
int yis320_initialize(imu_protocol_handler_t* handler);

/**
 * @brief Read data from YIS320 IMU
 *
 * Note: For YIS320, this is a no-op since data is processed
 * incrementally via process_rx_data callback.
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:117]
 *
 * @param handler IMU protocol handler instance
 * @param imu_state Output IMU state structure
 * @return true if new data was decoded
 */
bool yis320_read_data(imu_protocol_handler_t* handler, imu_state_t* imu_state);

/**
 * @brief Check if YIS320 IMU is connected
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:125]
 *
 * @param handler IMU protocol handler instance
 * @return true if connected
 */
bool yis320_is_connected(imu_protocol_handler_t* handler);

/**
 * @brief Close YIS320 IMU connection
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:132]
 *
 * @param handler IMU protocol handler instance
 */
void yis320_close(imu_protocol_handler_t* handler);

/**
 * @brief Process received UART data
 *
 * This function is called from UART RX interrupt context.
 * It processes the data and:
 * 1. Updates local imu_state_t
 * 2. Converts to imu_state_ipc_t
 * 3. Writes to shared memory via gateway_write_imu_state()
 * 4. Notifies Core 0 via gateway_notify_imu_ready()
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:144]
 * [MODIFIED: Added IPC integration for Core 1 -> Core 0 communication]
 *
 * @param handler IMU protocol handler instance
 * @param data Received data buffer
 * @param length Data length
 */
void yis320_process_rx_data(imu_protocol_handler_t* handler, const uint8_t* data, uint16_t length);

/*==============================================================================
 * IPC CONVERSION FUNCTIONS
 *============================================================================*/

/**
 * @brief Convert imu_state_t to imu_state_ipc_t for IPC
 *
 * Converts local IMU state to IPC format for shared memory transfer.
 * Maps:
 * - imu_state_t.gyroscope[3] -> imu_state_ipc_t.gyro[3]
 * - imu_state_t.quaternion[4] -> imu_state_ipc_t.quat[4]
 * - imu_state_t.rpy[3] -> imu_state_ipc_t.euler[3]
 *
 * [NEW: Added for IPC integration]
 *
 * @param src Source imu_state_t structure
 * @param dst Destination imu_state_ipc_t structure
 */
void yis320_convert_to_ipc(const imu_state_t* src, imu_state_ipc_t* dst);

/*==============================================================================
 * PLATFORM-SPECIFIC FUNCTIONS (Implemented in imu_interface.c)
 *============================================================================*/

/**
 * @brief Configure UART for YIS320 IMU
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:157]
 *
 * @param uart_port UART port number
 * @param baud_rate Baud rate (typically 921600)
 * @return 0 on success, -1 on error
 */
int yis320_uart_configure(uint8_t uart_port, uint32_t baud_rate);

/**
 * @brief Send data to YIS320 IMU via UART
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:167]
 *
 * @param uart_port UART port number
 * @param data Data to send
 * @param length Data length
 * @return Number of bytes sent, or -1 on error
 */
int yis320_uart_send(uint8_t uart_port, const uint8_t* data, uint16_t length);

/**
 * @brief Get current timestamp in milliseconds
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.h:174]
 *
 * @return Timestamp in milliseconds
 */
uint32_t yis320_get_timestamp_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* CCU_TI_YIS320_PROTOCOL_H_ */
