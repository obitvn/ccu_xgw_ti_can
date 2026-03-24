/**
 * @file imu_interface.h
 * @brief IMU UART interface for TI AM263Px
 *
 * Platform-specific UART interface for IMU communication
 * Uses USER_INTR mode with custom ISR
 * RX data processed in UDP TX task (no separate IMU task)
 *
 * @author Chu Tien Thinh
 * @date 2025
 */

#ifndef CCU_TI_IMU_INTERFACE_H_
#define CCU_TI_IMU_INTERFACE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  /* For NULL */

/* Include UART driver for callback stubs */
#include <drivers/uart.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define IMU_UART_PORT           5       /* UART5 for IMU */
#define IMU_UART_BAUD_RATE      921600  /* YIS320 default baud rate */

/* Circular RX buffer size (power of 2 for efficient modulo)
 * YIS320 @ 400Hz: 11 bytes/packet * 400 = 4.4KB/s
 * 512 bytes gives ~100ms buffer before overflow */
#define IMU_RX_BUFFER_SIZE      512

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

/**
 * @brief IMU UART interface state
 */
typedef struct {
    bool     initialized;       /* Initialization flag */
    uint32_t rx_count;          /* RX byte count */
    uint32_t tx_count;          /* TX byte count */
    uint32_t error_count;       /* Error count */
} imu_uart_state_t;

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * @brief Initialize IMU UART interface
 *
 * Configures UART5 for YIS320 IMU communication:
 * - Baudrate: 921600
 * - Data bits: 8
 * - Stop bits: 1
 * - Parity: None
 * - USER_INTR mode with custom ISR
 * - RX ISR fills circular buffer, processed in UDP TX task
 *
 * @return 0 on success, -1 on error
 */
int imu_uart_init(void);

/**
 * @brief Send data to IMU via UART
 *
 * @param data Data buffer to send
 * @param length Data length
 * @return Number of bytes sent, or -1 on error
 */
int imu_uart_send(const uint8_t* data, uint16_t length);

/**
 * @brief Check if IMU UART is initialized
 *
 * @return true if initialized
 */
bool imu_uart_is_initialized(void);

/**
 * @brief Get IMU UART statistics
 *
 * @param state Output state structure
 */
void imu_uart_get_state(imu_uart_state_t* state);

/**
 * @brief Deinitialize IMU UART interface
 *
 * Cancels transfers and releases resources.
 */
void imu_uart_deinit(void);

/**
 * @brief Process IMU data from circular buffer
 *
 * Called from UDP TX task to process any pending IMU data
 * Uses critical section to safely access circular buffer
 */
void imu_uart_process_rx_data(void);

/*==============================================================================
 * STUB CALLBACKS (for syscfg compatibility, not used in USER_INTR mode)
 *============================================================================*/

/**
 * @brief Stub RX callback (not used in USER_INTR mode)
 */
void imu_uart_read_callback(UART_Handle handle, UART_Transaction *trans);

/**
 * @brief Stub TX callback (not used in USER_INTR mode)
 */
void imu_uart_write_callback(UART_Handle handle, UART_Transaction *trans);

/*==============================================================================
 * YIS320 PROTOCOL PLATFORM FUNCTIONS
 *============================================================================*/

/**
 * @brief Configure UART for YIS320 IMU
 *
 * @param uart_port UART port number
 * @param baud_rate Baud rate
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

/*==============================================================================
 * SHARED DATA (accessed by UDP TX task)
 *============================================================================*/

/**
 * @brief Circular RX buffer (filled by ISR, read by UDP TX task)
 */
extern uint8_t g_imu_rx_buffer[IMU_RX_BUFFER_SIZE];
extern volatile uint16_t g_imu_rx_head;  /* Write position (ISR) */
extern volatile uint16_t g_imu_rx_tail;  /* Read position (UDP TX task) */

#ifdef __cplusplus
}
#endif

#endif /* CCU_TI_IMU_INTERFACE_H_ */
