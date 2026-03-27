/**
 * @file motor_mapping.c
 * @brief Motor mapping implementation for CCU Multicore Gateway - Core 0 (FreeRTOS)
 *
 * This file provides read-only access to motor configuration data stored in
 * shared memory by Core 1 (NoRTOS). Core 0 uses this to query motor mappings
 * for Ethernet gateway operations.
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c]
 *
 * Core 0 (FreeRTOS) Role:
 * - Read-only access to motor configuration tables
 * - Queries motor limits and configuration by index
 * - No modification of configuration data
 * - Waits for Core 1 to populate shared memory at startup
 *
 * Architecture:
 * - Motor limits and configuration tables are stored in shared memory by Core 1
 * - Core 0 accesses these tables via gateway_shared.h interface
 * - O(1) lookup table for fast motor_id+can_bus -> motor_idx mapping
 *
 * @author CCU Multicore Project
 * @date 2026-03-27
 */

#include "motor_mapping.h"
#include "../gateway_shared.h"
#include <kernel/dpl/DebugP.h>
#include <string.h>

/*==============================================================================
 * SHARED MEMORY REFERENCES
 *============================================================================*/

/**
 * @brief Pointer to O(1) motor lookup table in shared memory
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:112]
 *
 * Maps (motor_id, can_bus) -> motor_idx for constant-time lookup.
 * - motor_id: 0-127 (128 entries)
 * - can_bus: 0-7 (8 entries per motor_id)
 * - Value: motor index (0-22) or 0xFF if not found
 *
 * Memory: 128 * 8 = 1024 bytes (1 KB)
 * Performance: O(1) instead of O(23) linear search
 *
 * CRITICAL for 1000Hz operation - saves ~23 * 22 = 506 comparisons per cycle!
 *
 * Core 1 builds this table at startup, Core 0 reads from it.
 */
extern const uint8_t g_motor_lookup[128][8];

/**
 * @brief Local motor configuration table for Core0
 *
 * Core0 builds this table from shared memory at startup.
 * This allows Core0 to use motor_config_t* pointers throughout the code
 * without accessing shared memory on every query.
 */
static motor_config_t g_local_motor_config_table[VD1_NUM_MOTORS] = {0};

/**
 * @brief Local motor limits table for Core0
 *
 * Core0 builds this table from shared memory at startup.
 * Index corresponds to motor_type_t enum values.
 */
static motor_limits_t g_local_motor_limits_table[7] = {0};  /* O0-O6 = 7 motor types */

/*==============================================================================
 * CORE 0 INITIALIZATION
 *============================================================================*/

/**
 * @brief Initialize motor mapping shared memory access on Core 0
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:197-227]
 *
 * Waits for Core 1 to populate the shared memory configuration tables.
 * Must be called once at startup before any motor mapping queries.
 *
 * Core 0 does NOT build the lookup table - Core 1 does that.
 * This function only verifies that shared memory is ready.
 *
 * @return 0 on success, -1 if timeout waiting for Core 1 initialization
 *
 * @note Thread-safe: Called once during Core 0 initialization
 * @note Blocks until Core 1 signals ready via gateway shared memory
 */
int motor_mapping_init_core0(void)
{
    DebugP_log("[Core0] Motor Mapping: Waiting for Core 1 to populate shared memory...\r\n");

    /* Wait for Core 1 to initialize motor configuration in shared memory */
    int ret = gateway_wait_motor_config_ready(5000);  /* 5 second timeout */

    if (ret != 0) {
        DebugP_log("[Core0] ERROR: Timeout waiting for motor configuration from Core 1\r\n");
        return -1;
    }

    DebugP_log("[Core0] Motor Mapping: Shared memory ready, motor configuration accessible\r\n");

    /* Build local motor configuration table from shared memory */
    for (uint8_t i = 0; i < VD1_NUM_MOTORS; i++) {
        const SharedMotorConfig_t* shared_config = gateway_get_motor_config(i);
        if (shared_config != NULL) {
            /* Convert SharedMotorConfig_t to motor_config_t */
            g_local_motor_config_table[i].motor_id = shared_config->motor_id;
            g_local_motor_config_table[i].can_bus = shared_config->can_bus;
            g_local_motor_config_table[i].motor_type = shared_config->motor_type;
            g_local_motor_config_table[i].direction = shared_config->direction;

            /* Convert individual limit fields to motor_limits_t struct */
            g_local_motor_config_table[i].limits.p_min = shared_config->p_min;
            g_local_motor_config_table[i].limits.p_max = shared_config->p_max;
            g_local_motor_config_table[i].limits.v_min = shared_config->v_min;
            g_local_motor_config_table[i].limits.v_max = shared_config->v_max;
            g_local_motor_config_table[i].limits.kp_min = shared_config->kp_min;
            g_local_motor_config_table[i].limits.kp_max = shared_config->kp_max;
            g_local_motor_config_table[i].limits.kd_min = shared_config->kd_min;
            g_local_motor_config_table[i].limits.kd_max = shared_config->kd_max;
            g_local_motor_config_table[i].limits.t_min = shared_config->t_min;
            g_local_motor_config_table[i].limits.t_max = shared_config->t_max;

            /* Also populate limits table by motor type */
            if (shared_config->motor_type < 7) {
                g_local_motor_limits_table[shared_config->motor_type].p_min = shared_config->p_min;
                g_local_motor_limits_table[shared_config->motor_type].p_max = shared_config->p_max;
                g_local_motor_limits_table[shared_config->motor_type].v_min = shared_config->v_min;
                g_local_motor_limits_table[shared_config->motor_type].v_max = shared_config->v_max;
                g_local_motor_limits_table[shared_config->motor_type].kp_min = shared_config->kp_min;
                g_local_motor_limits_table[shared_config->motor_type].kp_max = shared_config->kp_max;
                g_local_motor_limits_table[shared_config->motor_type].kd_min = shared_config->kd_min;
                g_local_motor_limits_table[shared_config->motor_type].kd_max = shared_config->kd_max;
                g_local_motor_limits_table[shared_config->motor_type].t_min = shared_config->t_min;
                g_local_motor_limits_table[shared_config->motor_type].t_max = shared_config->t_max;
            }
        }
    }

    /* Verify lookup table is populated by checking a few entries */
    uint8_t found_count = 0;
    for (uint8_t i = 0; i < VD1_NUM_MOTORS; i++) {
        if (g_local_motor_config_table[i].motor_id < 128 &&
            g_local_motor_config_table[i].can_bus < 8) {
            uint8_t motor_id = g_local_motor_config_table[i].motor_id;
            uint8_t can_bus = g_local_motor_config_table[i].can_bus;
            uint8_t idx = g_motor_lookup[motor_id][can_bus];
            if (idx == i) {
                found_count++;
            }
        }
    }

    DebugP_log("[Core0] Motor Mapping: Verified %u/%u motors in lookup table\r\n",
               found_count, VD1_NUM_MOTORS);

    if (found_count != VD1_NUM_MOTORS) {
        DebugP_log("[Core0] WARNING: Not all motors found in lookup table!\r\n");
    }

    return 0;
}

/*==============================================================================
 * PUBLIC API - READ-ONLY ACCESS TO SHARED MEMORY
 *============================================================================*/

/**
 * @brief Get motor limits by type (read-only from shared memory)
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:167-185]
 *
 * @param type Motor type
 * @return Pointer to motor limits structure in shared memory, or NULL if invalid
 *
 * @note This function reads from shared memory populated by Core 1
 * @note Thread-safe for read-only access on Core 0
 */
const motor_limits_t* motor_get_limits(motor_type_t type)
{
    if (type >= MOTOR_TYPE_UNKNOWN || type >= 7) {
        return NULL;
    }

    return &g_local_motor_limits_table[type];
}

/**
 * @brief Get motor configuration by index (read-only from local table)
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:187-193]
 *
 * @param index Motor index (0-22)
 * @return Pointer to motor configuration structure, or NULL if invalid
 *
 * @note This returns from local table populated at init from shared memory
 * @note Thread-safe for read-only access on Core 0
 */
const motor_config_t* motor_get_config(uint8_t index)
{
    if (index >= VD1_NUM_MOTORS) {
        return NULL;
    }

    return &g_local_motor_config_table[index];
}

/*==============================================================================
 * UTILITY FUNCTIONS
 *============================================================================*/

/**
 * @brief Convert uint16 to float with range
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:233-238]
 *
 * @param x Input value
 * @param x_min Minimum output
 * @param x_max Maximum output
 * @param bits Number of bits
 * @return Converted float value
 */
float uint16_to_float(uint16_t x, float x_min, float x_max, int bits)
{
    uint32_t span = (1U << bits) - 1;
    float offset = x_max - x_min;
    return offset * ((float)x) / ((float)span) + x_min;
}

/**
 * @brief Convert float to uint16 with range
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:240-247]
 *
 * @param x Input value
 * @param x_min Minimum input
 * @param x_max Maximum input
 * @param bits Number of bits
 * @return Converted uint16 value
 */
int float_to_uint(float x, float x_min, float x_max, int bits)
{
    float span = x_max - x_min;
    float offset = x_min;
    if (x > x_max) x = x_max;
    else if (x < x_min) x = x_min;
    return (int)(((x - offset) * ((float)((1 << bits) - 1))) / span);
}

/**
 * @brief Convert 4 bytes to float (little-endian)
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:249-261]
 *
 * @param bytedata Input byte array
 * @return Float value
 */
float bytes_to_float(const uint8_t* bytedata)
{
    uint32_t data = ((uint32_t)bytedata[3] << 24) |
                    ((uint32_t)bytedata[2] << 16) |
                    ((uint32_t)bytedata[1] << 8) |
                    (uint32_t)bytedata[0];
    union {
        uint32_t u32;
        float    f;
    } converter;
    converter.u32 = data;
    return converter.f;
}

