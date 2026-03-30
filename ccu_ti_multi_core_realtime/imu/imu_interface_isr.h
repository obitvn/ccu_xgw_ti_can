/**
 * @file imu_interface_isr.h
 * @brief IMU UART interface with Parse-in-ISR for TI AM263Px
 *
 * Parse YIS320 IMU frames directly in UART ISR for lowest latency.
 * No separate IMU RX task needed.
 *
 * @author Chu Tien Thinh
 * @date 2025
 */

#ifndef CCU_TI_IMU_INTERFACE_ISR_H_
#define CCU_TI_IMU_INTERFACE_ISR_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Include UART driver for callback stubs */
#include <drivers/uart.h>
/* Include motor_mapping for imu_state_t definition */
#include "../motor_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define IMU_UART_PORT           5       /* UART5 for IMU */
#define IMU_UART_BAUD_RATE      921600  /* YIS320 default baud rate */

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
 * @brief Initialize IMU UART interface with Parse-in-ISR
 *
 * Configures UART5 for YIS320 IMU communication:
 * - Baudrate: 921600
 * - Data bits: 8
 * - Stop bits: 1
 * - Parity: None
 * - USER_INTR mode with custom ISR that parses frames
 *
 * @return 0 on success, -1 on error
 */
int imu_uart_isr_init(void);

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
 */
void imu_uart_deinit(void);

/**
 * @brief Get current IMU state (atomically from ISR)
 *
 * This function reads the IMU state that was updated by ISR.
 * Safe to call from any task (typically UDP TX task).
 *
 * @param imu_state Output IMU state structure
 * @return true if new data is available
 */
bool imu_protocol_get_state_isr(imu_state_t* imu_state);

/**
 * @brief Log ISR statistics (call periodically)
 *
 * Call this function periodically (e.g., every 5 seconds) from
 * a task to log ISR timing statistics.
 */
void imu_uart_isr_log_stats(void);

/**
 * @brief Process pending IMU IPC notification (call from main loop)
 *
 * BUG B010 FIX: This function checks if IMU data is ready and sends
 * IPC notification to Core0 in task context (safe from spinlock deadlock).
 * Should be called from the main loop, NOT from ISR context.
 *
 * @return true if notification was sent, false otherwise
 */
bool imu_uart_process_ipc_notification(void);

/*==============================================================================
 * STUB CALLBACKS (for syscfg compatibility)
 *============================================================================*/

/**
 * @brief Stub RX callback (not used in ISR mode)
 */
void imu_uart_read_callback(UART_Handle handle, UART_Transaction *trans);

/**
 * @brief Stub TX callback (not used in ISR mode)
 */
void imu_uart_write_callback(UART_Handle handle, UART_Transaction *trans);

#ifdef __cplusplus
}
#endif

#endif /* CCU_TI_IMU_INTERFACE_ISR_H_ */
