/**
 * @file yis320_protocol.h
 * @brief YIS320 IMU protocol implementation for ccu_ti xGW Gateway
 *
 * Adapted from freertos_xgw for TI AM263Px
 *
 * @author Adapted from freertos_xgw by Chu Tien Thinh
 * @date 2025
 */

#ifndef CCU_TI_YIS320_PROTOCOL_H_
#define CCU_TI_YIS320_PROTOCOL_H_

#include "../imu_protocol_handler.h"
#include "../../motor_mapping.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  /* For NULL */

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
#define YIS320_DEG_TO_RAD        (3.14159265358979323846f / 180.0f)
// Data factor for NOT_MAG_DATA
#define YIS320_DATA_FACTOR       0.000001f

// YIS320 Data IDs
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
 */
extern yis320_private_data_t g_yis320_private_data;

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * @brief Initialize YIS320 protocol handler
 *
 * @param handler IMU protocol handler instance to initialize
 * @return 0 on success, -1 on error
 */
int yis320_protocol_handler_init(imu_protocol_handler_t* handler);

/**
 * @brief Initialize YIS320 IMU hardware connection
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
 * @param handler IMU protocol handler instance
 * @param imu_state Output IMU state structure
 * @return true if new data was decoded
 */
bool yis320_read_data(imu_protocol_handler_t* handler, imu_state_t* imu_state);

/**
 * @brief Check if YIS320 IMU is connected
 *
 * @param handler IMU protocol handler instance
 * @return true if connected
 */
bool yis320_is_connected(imu_protocol_handler_t* handler);

/**
 * @brief Close YIS320 IMU connection
 *
 * @param handler IMU protocol handler instance
 */
void yis320_close(imu_protocol_handler_t* handler);

/**
 * @brief Process received UART data
 *
 * This function is called from UART RX interrupt context.
 * It processes the data and updates the shared IMU state buffer.
 *
 * @param handler IMU protocol handler instance
 * @param data Received data buffer
 * @param length Data length
 */
void yis320_process_rx_data(imu_protocol_handler_t* handler, const uint8_t* data, uint16_t length);

/*==============================================================================
 * PLATFORM-SPECIFIC FUNCTIONS (Implemented in imu_interface.c)
 *============================================================================*/

/**
 * @brief Configure UART for YIS320 IMU
 *
 * @param uart_port UART port number
 * @param baud_rate Baud rate (typically 921600)
 * @return 0 on success, -1 on error
 */
int yis320_uart_configure(uint8_t uart_port, uint32_t baud_rate);

/**
 * @brief Send data to YIS320 IMU via UART
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
 * @return Timestamp in milliseconds
 */
uint32_t yis320_get_timestamp_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* CCU_TI_YIS320_PROTOCOL_H_ */
