/**
 * @file motor_mapping.h
 * @brief Motor mapping configuration for CCU Multicore Gateway - Core 0 (FreeRTOS)
 *
 * This header provides read-only access to motor configuration data stored in
 * shared memory by Core 1 (NoRTOS). Core 0 uses this to query motor mappings
 * for Ethernet gateway operations.
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:217-262]
 *
 * Core 0 (FreeRTOS) Role:
 * - Read-only access to motor configuration tables
 * - Queries motor limits and configuration by index
 * - No modification of configuration data
 *
 * @author CCU Multicore Project
 * @date 2026-03-27
 */

#ifndef MOTOR_MAPPING_FREERTOS_H_
#define MOTOR_MAPPING_FREERTOS_H_

#include <stdint.h>
#include <stdbool.h>
#include "../common/motor_config_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * SHARED MEMORY ACCESS
 *============================================================================*/

/**
 * @brief Get motor limits by type (read-only from shared memory)
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:220-225]
 *
 * @param type Motor type
 * @return Pointer to motor limits structure in shared memory, or NULL if invalid
 *
 * @note This function reads from shared memory populated by Core 1
 * @note Thread-safe for read-only access on Core 0
 */
const motor_limits_t* motor_get_limits(motor_type_t type);

/**
 * @brief Get motor configuration by index (read-only from shared memory)
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:227-232]
 *
 * @param index Motor index (0-22)
 * @return Pointer to motor configuration structure in shared memory, or NULL if invalid
 *
 * @note This function reads from shared memory populated by Core 1
 * @note Thread-safe for read-only access on Core 0
 */
const motor_config_t* motor_get_config(uint8_t index);

/**
 * @brief Get motor index by CAN ID and bus (O(1) lookup table)
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:234-254]
 *
 * @param motor_id CAN motor ID
 * @param can_bus CAN bus number
 * @return Motor index (0-22) or 0xFF if not found
 *
 * @note This is now O(1) using lookup table instead of O(n) linear search.
 * @note Critical for 1000Hz operation - called 23 times per cycle!
 * @note Implementation is inline for maximum performance.
 * @note Reads lookup table from shared memory populated by Core 1.
 */
static inline uint8_t motor_get_index(uint8_t motor_id, uint8_t can_bus)
{
    /* External reference to lookup table in shared memory */
    extern const uint8_t g_motor_lookup[128][8];

    if (motor_id < 128 && can_bus < 8) {
        return g_motor_lookup[motor_id][can_bus];
    }
    return 0xFF;  /* Not found - invalid input */
}

/**
 * @brief Initialize motor mapping shared memory access on Core 0
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:256-262]
 *
 * Waits for Core 1 to populate the shared memory configuration tables.
 * Must be called once at startup before any motor mapping queries.
 *
 * @return 0 on success, -1 if timeout waiting for Core 1 initialization
 *
 * @note Core 0 does NOT build the lookup table - Core 1 does that
 * @note This function only verifies that shared memory is ready
 */
int motor_mapping_init_core0(void);

/*==============================================================================
 * UTILITY FUNCTIONS
 *============================================================================*/

/**
 * @brief Convert uint16 to float with range
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:268-276]
 *
 * @param x Input value
 * @param x_min Minimum output
 * @param x_max Maximum output
 * @param bits Number of bits
 * @return Converted float value
 */
float uint16_to_float(uint16_t x, float x_min, float x_max, int bits);

/**
 * @brief Convert float to uint16 with range
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:278-286]
 *
 * @param x Input value
 * @param x_min Minimum input
 * @param x_max Maximum input
 * @param bits Number of bits
 * @return Converted uint16 value
 */
int float_to_uint(float x, float x_min, float x_max, int bits);

/**
 * @brief Convert 4 bytes to float (little-endian)
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:288-293]
 *
 * @param bytedata Input byte array
 * @return Float value
 */
float bytes_to_float(const uint8_t* bytedata);

/* Note: CRC32 function is now in common/crc32.h - include that header if needed */

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_MAPPING_FREERTOS_H_ */
