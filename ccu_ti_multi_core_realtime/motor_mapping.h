/**
 * @file motor_mapping.h
 * @brief Motor mapping configuration for CCU Multicore Gateway - Core 1 (NoRTOS)
 *
 * Core 1 (NoRTOS) is the owner and maintainer of motor configuration data.
 * This module builds and populates the shared memory configuration tables
 * that Core 0 (FreeRTOS) queries for read-only access.
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:1-315]
 *
 * Core 1 (NoRTOS) Role:
 * - Initialize and build motor configuration tables
 * - Populate shared memory with configuration data
 * - Maintain O(1) lookup table for motor_id+can_bus -> motor_idx mapping
 * - Bare metal constraints: no dynamic allocation, no OS calls
 *
 * @author CCU Multicore Project
 * @date 2026-03-27
 */

#ifndef MOTOR_MAPPING_REALTIME_H_
#define MOTOR_MAPPING_REALTIME_H_

#include <stdint.h>
#include <stdbool.h>
#include "../common/motor_config_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

/* Lookup table dimensions for O(1) motor index lookup */
#define MOTOR_LOOKUP_ID_MAX    128    /* Motor ID range (0-127) */
#define MOTOR_LOOKUP_BUS_MAX   8      /* CAN bus range (0-7) */

/*==============================================================================
 * PUBLIC API - CORE 1 (Owner/Maintainer)
 *============================================================================*/

/**
 * @brief Initialize motor mapping tables and populate shared memory
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:256-262]
 *
 * This function builds the O(1) lookup table for fast motor_id+can_bus -> motor_idx
 * mapping and copies the configuration data to shared memory for Core 0 access.
 *
 * Must be called once at startup on Core 1 before any CAN operations.
 *
 * @note This is a CORE 1 ONLY function - Core 0 must use motor_mapping_init_core0()
 * @note Bare metal compliant: no dynamic allocation, no OS calls
 * @return 0 on success, -1 on error
 */
int motor_mapping_init_core1(void);

/**
 * @brief Get motor limits by type
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:220-225]
 *
 * @param type Motor type
 * @return Pointer to motor limits structure
 */
const motor_limits_t* motor_get_limits(motor_type_t type);

/**
 * @brief Get motor configuration by index
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:227-232]
 *
 * @param index Motor index (0-22)
 * @return Pointer to motor configuration structure
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
 * @note Implementation is inline in header for maximum performance.
 */
static inline uint8_t motor_get_index(uint8_t motor_id, uint8_t can_bus)
{
    /* External reference to lookup table defined in motor_mapping.c */
    extern uint8_t g_motor_lookup[MOTOR_LOOKUP_ID_MAX][MOTOR_LOOKUP_BUS_MAX];

    if (motor_id < MOTOR_LOOKUP_ID_MAX && can_bus < MOTOR_LOOKUP_BUS_MAX) {
        return g_motor_lookup[motor_id][can_bus];
    }
    return 0xFF;  /* Not found - invalid input */
}

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

/**
 * @brief Calculate CRC32 checksum using table-driven algorithm
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:295-309]
 *
 * Fast table-driven CRC32 implementation matching Python's binascii.crc32.
 * Uses IEEE 802.3 polynomial (0xEDB88320 reversed).
 *
 * Performance: ~10-20us for 500 bytes (vs ~200-400us byte-by-byte)
 * Memory: 1KB lookup table (stored in flash)
 * Speedup: ~10-20x faster than byte-by-byte implementation
 *
 * @param ptr Input data pointer (uint8_t*)
 * @param len Length in bytes
 * @return CRC32 value (IEEE 802.3 standard)
 */
uint32_t crc32_core(const uint8_t* ptr, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_MAPPING_REALTIME_H_ */
