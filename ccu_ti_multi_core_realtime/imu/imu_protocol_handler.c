/**
 * @file imu_protocol_handler.c
 * @brief IMU protocol handler implementation for ccu_ti
 *
 * Adapted from freertos_xgw for TI AM263Px
 *
 * @author Adapted from freertos_xgw by Chu Tien Thinh
 * @date 2025
 */

#include "imu_protocol_handler.h"
#include "yis320/yis320_protocol.h"
#include <string.h>
#include <stddef.h>  /* For NULL */
#include <kernel/dpl/HwiP.h>  /* For HwiP_disable/restore */

/*==============================================================================
 * CRITICAL SECTION WRAPPERS (NoRTOS)
 *============================================================================*/
#define IMU_ENTER_CRITICAL()    uintptr_t imu_cs_key = HwiP_disable()
#define IMU_EXIT_CRITICAL()     HwiP_restore(imu_cs_key)

/*==============================================================================
 * INTERNAL DATA
 *============================================================================*/

/**
 * @brief Global IMU protocol handler instance
 */
static imu_protocol_handler_t g_imu_handler;

/**
 * @brief Shared IMU state buffer
 * Updated by UART RX callback, read by UDP TX task
 * Protected by critical section in FreeRTOS
 */
static imu_state_t g_imu_state;

/**
 * @brief Initialization flag
 */
static bool g_imu_manager_initialized = false;

/*==============================================================================
 * PUBLIC FUNCTIONS
 *============================================================================*/

int imu_protocol_handler_init(imu_protocol_handler_t* handler, imu_type_t imu_type)
{
    if (handler == NULL) {
        return -1;
    }

    handler->imu_type = imu_type;
    handler->private_data = NULL;

    switch (imu_type) {
        case IMU_TYPE_YIS320:
            return yis320_protocol_handler_init(handler);

        default:
            return -1;  // Unknown IMU type
    }
}

int imu_protocol_manager_init(imu_type_t imu_type)
{
    if (g_imu_manager_initialized) {
        return 0;  // Already initialized
    }

    // Initialize IMU state buffer
    memset(&g_imu_state, 0, sizeof(g_imu_state));
    g_imu_state.updated = false;

    // Initialize protocol handler
    if (imu_protocol_handler_init(&g_imu_handler, imu_type) != 0) {
        return -1;
    }

    // Initialize the IMU hardware
    if (imu_initialize(&g_imu_handler) != 0) {
        return -1;
    }

    g_imu_manager_initialized = true;
    return 0;
}

bool imu_protocol_get_state(imu_state_t* imu_state)
{
    if (!g_imu_manager_initialized || imu_state == NULL) {
        return false;
    }

    // Copy current IMU state (thread-safe in FreeRTOS)
    *imu_state = g_imu_state;

    // Clear updated flag so next call returns false until new data arrives
    bool was_updated = g_imu_state.updated;
    g_imu_state.updated = false;

    return was_updated;
}

void imu_protocol_process_uart_rx(const uint8_t* data, uint16_t length)
{
    /* [TEMP FIX] Bypass g_imu_manager_initialized check to force parsing */
    if (data == NULL || length == 0) {
        return;
    }

    /* Force initialization if not already done */
    if (!g_imu_manager_initialized) {
        /* Try to initialize - YIS320 = 0 */
        if (imu_protocol_handler_init(&g_imu_handler, (imu_type_t)0) == 0) {
            g_imu_manager_initialized = true;
        }
    }

    // Process received data through protocol handler
    imu_process_rx_data(&g_imu_handler, data, length);
}

bool imu_protocol_is_data_updated(void)
{
    return g_imu_state.updated;
}

void imu_protocol_clear_updated_flag(void)
{
    g_imu_state.updated = false;
}

/*==============================================================================
 * INTERNAL CALLBACK FOR PROTOCOL IMPLEMENTATIONS
 *============================================================================*/

/**
 * @brief Update shared IMU state buffer
 * Called by protocol implementations when new data is decoded
 *
 * @param imu_state New IMU state to store
 */
void imu_protocol_update_state(const imu_state_t* imu_state)
{
    if (imu_state != NULL) {
        // Use critical section for thread safety (called from ISR context)
        IMU_ENTER_CRITICAL();
        g_imu_state = *imu_state;
        g_imu_state.updated = true;
        IMU_EXIT_CRITICAL();
    }
}
