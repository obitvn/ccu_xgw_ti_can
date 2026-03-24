/**
 * @file imu_protocol_handler.h
 * @brief IMU protocol handler interface for ccu_ti xGW Gateway
 *
 * Adapted from freertos_xgw for TI AM263Px
 *
 * @author Adapted from freertos_xgw by Chu Tien Thinh
 * @date 2025
 */

#ifndef CCU_TI_IMU_PROTOCOL_HANDLER_H_
#define CCU_TI_IMU_PROTOCOL_HANDLER_H_

#include "../motor_mapping.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  /* For NULL */

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

/**
 * @brief Forward declaration for IMU protocol handler
 */
typedef struct imu_protocol_handler imu_protocol_handler_t;

/**
 * @brief IMU protocol handler virtual function table (vtable)
 */
typedef struct {
    /**
     * @brief Initialize IMU connection
     * @param handler IMU protocol handler instance
     * @return 0 on success, -1 on error
     */
    int (*initialize)(imu_protocol_handler_t* handler);

    /**
     * @brief Read and decode data from IMU
     * @param handler IMU protocol handler instance
     * @param imu_state Output IMU state structure
     * @return true if new data was read, false otherwise
     */
    bool (*read_data)(imu_protocol_handler_t* handler, imu_state_t* imu_state);

    /**
     * @brief Check if IMU is connected
     * @param handler IMU protocol handler instance
     * @return true if connected
     */
    bool (*is_connected)(imu_protocol_handler_t* handler);

    /**
     * @brief Close/Deinit IMU connection
     * @param handler IMU protocol handler instance
     */
    void (*deinit)(imu_protocol_handler_t* handler);

    /**
     * @brief Process received UART data (called from UART RX callback)
     * @param handler IMU protocol handler instance
     * @param data Received data buffer
     * @param length Data length
     */
    void (*process_rx_data)(imu_protocol_handler_t* handler, const uint8_t* data, uint16_t length);

} imu_protocol_vtable_t;

/**
 * @brief IMU protocol handler structure
 */
struct imu_protocol_handler {
    const imu_protocol_vtable_t* vtable;  // Virtual function table
    imu_type_t imu_type;                  // IMU type
    void* private_data;                   // Protocol-specific private data
};

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * @brief Initialize IMU protocol handler
 * @param handler IMU protocol handler instance
 * @param imu_type IMU type
 * @return 0 on success, -1 on error
 */
int imu_protocol_handler_init(imu_protocol_handler_t* handler, imu_type_t imu_type);

/**
 * @brief Initialize IMU (wrapper)
 */
static inline int imu_initialize(imu_protocol_handler_t* handler)
{
    return handler->vtable->initialize(handler);
}

/**
 * @brief Read IMU data (wrapper)
 */
static inline bool imu_read_data(imu_protocol_handler_t* handler, imu_state_t* imu_state)
{
    return handler->vtable->read_data(handler, imu_state);
}

/**
 * @brief Check if IMU is connected (wrapper)
 */
static inline bool imu_is_connected(imu_protocol_handler_t* handler)
{
    return handler->vtable->is_connected(handler);
}

/**
 * @brief Close/Deinit IMU connection (wrapper)
 */
static inline void imu_close(imu_protocol_handler_t* handler)
{
    handler->vtable->deinit(handler);
}

/**
 * @brief Process UART RX data (wrapper)
 * Called from UART RX interrupt context
 */
static inline void imu_process_rx_data(imu_protocol_handler_t* handler, const uint8_t* data, uint16_t length)
{
    if (handler->vtable->process_rx_data != NULL) {
        handler->vtable->process_rx_data(handler, data, length);
    }
}

/*==============================================================================
 * IMU MANAGER FUNCTIONS
 *============================================================================*/

/**
 * @brief Initialize IMU protocol manager
 * @param imu_type IMU type to use
 * @return 0 on success, -1 on error
 */
int imu_protocol_manager_init(imu_type_t imu_type);

/**
 * @brief Get current IMU state
 *
 * This function is called by UDP TX task to retrieve current IMU state.
 *
 * @param imu_state Output IMU state structure
 * @return true if data is available and recent
 */
bool imu_protocol_get_state(imu_state_t* imu_state);

/**
 * @brief Process UART RX data (called from UART RX callback)
 *
 * This function is called from UART RX interrupt for each received data.
 * It processes the data and updates the shared IMU state buffer.
 *
 * @param data Received data buffer
 * @param length Data length
 */
void imu_protocol_process_uart_rx(const uint8_t* data, uint16_t length);

/**
 * @brief Check if IMU data has been updated
 * @return true if new IMU data is available
 */
bool imu_protocol_is_data_updated(void);

/**
 * @brief Clear IMU data updated flag
 */
void imu_protocol_clear_updated_flag(void);

/**
 * @brief Update shared IMU state buffer
 * Internal function used by protocol implementations
 *
 * @param imu_state New IMU state to store
 */
void imu_protocol_update_state(const imu_state_t* imu_state);

#ifdef __cplusplus
}
#endif

#endif /* CCU_TI_IMU_PROTOCOL_HANDLER_H_ */
