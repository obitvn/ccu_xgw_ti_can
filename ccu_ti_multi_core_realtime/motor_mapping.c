/**
 * @file motor_mapping.c
 * @brief Motor mapping implementation for CCU Multicore Gateway - Core 1 (NoRTOS)
 *
 * Core 1 (NoRTOS) owns and maintains all motor configuration data.
 * This module builds the lookup tables and populates shared memory
 * for read-only access by Core 0 (FreeRTOS).
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:1-351]
 *
 * Bare Metal Constraints:
 * - No dynamic memory allocation (all tables are static const)
 * - No OS calls or blocking operations
 * - Deterministic execution time
 * - All data in flash/ROM or static RAM
 *
 * @author CCU Multicore Project
 * @date 2026-03-27
 */

#include "motor_mapping.h"
#include <string.h>
#include <kernel/dpl/DebugP.h>
#include "../gateway_shared.h"
#include "../common/motor_config_types.h"

/*==============================================================================
 * MOTOR LIMITS TABLES (Flash/ROM)
 *============================================================================*/

/**
 * @brief Robstride O0 motor limits
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:15-26]
 */
static const motor_limits_t robstride_o0_limits = {
    .p_min = ROBSTRIDE_O0_P_MIN,
    .p_max = ROBSTRIDE_O0_P_MAX,
    .v_min = ROBSTRIDE_O0_V_MIN,
    .v_max = ROBSTRIDE_O0_V_MAX,
    .kp_min = ROBSTRIDE_O0_KP_MIN,
    .kp_max = ROBSTRIDE_O0_KP_MAX,
    .kd_min = ROBSTRIDE_O0_KD_MIN,
    .kd_max = ROBSTRIDE_O0_KD_MAX,
    .t_min = ROBSTRIDE_O0_T_MIN,
    .t_max = ROBSTRIDE_O0_T_MAX
};

/**
 * @brief Robstride O2 motor limits
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:28-39]
 */
static const motor_limits_t robstride_o2_limits = {
    .p_min = ROBSTRIDE_O2_P_MIN,
    .p_max = ROBSTRIDE_O2_P_MAX,
    .v_min = ROBSTRIDE_O2_V_MIN,
    .v_max = ROBSTRIDE_O2_V_MAX,
    .kp_min = ROBSTRIDE_O2_KP_MIN,
    .kp_max = ROBSTRIDE_O2_KP_MAX,
    .kd_min = ROBSTRIDE_O2_KD_MIN,
    .kd_max = ROBSTRIDE_O2_KD_MAX,
    .t_min = ROBSTRIDE_O2_T_MIN,
    .t_max = ROBSTRIDE_O2_T_MAX
};

/**
 * @brief Robstride O3 motor limits
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:41-52]
 */
static const motor_limits_t robstride_o3_limits = {
    .p_min = ROBSTRIDE_O3_P_MIN,
    .p_max = ROBSTRIDE_O3_P_MAX,
    .v_min = ROBSTRIDE_O3_V_MIN,
    .v_max = ROBSTRIDE_O3_V_MAX,
    .kp_min = ROBSTRIDE_O3_KP_MIN,
    .kp_max = ROBSTRIDE_O3_KP_MAX,
    .kd_min = ROBSTRIDE_O3_KD_MIN,
    .kd_max = ROBSTRIDE_O3_KD_MAX,
    .t_min = ROBSTRIDE_O3_T_MIN,
    .t_max = ROBSTRIDE_O3_T_MAX
};

/**
 * @brief Robstride O4 motor limits
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:54-65]
 */
static const motor_limits_t robstride_o4_limits = {
    .p_min = ROBSTRIDE_O4_P_MIN,
    .p_max = ROBSTRIDE_O4_P_MAX,
    .v_min = ROBSTRIDE_O4_V_MIN,
    .v_max = ROBSTRIDE_O4_V_MAX,
    .kp_min = ROBSTRIDE_O4_KP_MIN,
    .kp_max = ROBSTRIDE_O4_KP_MAX,
    .kd_min = ROBSTRIDE_O4_KD_MIN,
    .kd_max = ROBSTRIDE_O4_KD_MAX,
    .t_min = ROBSTRIDE_O4_T_MIN,
    .t_max = ROBSTRIDE_O4_T_MAX
};

/**
 * @brief Robstride O5 motor limits
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:67-78]
 */
static const motor_limits_t robstride_o5_limits = {
    .p_min = ROBSTRIDE_O5_P_MIN,
    .p_max = ROBSTRIDE_O5_P_MAX,
    .v_min = ROBSTRIDE_O5_V_MIN,
    .v_max = ROBSTRIDE_O5_V_MAX,
    .kp_min = ROBSTRIDE_O5_KP_MIN,
    .kp_max = ROBSTRIDE_O5_KP_MAX,
    .kd_min = ROBSTRIDE_O5_KD_MIN,
    .kd_max = ROBSTRIDE_O5_KD_MAX,
    .t_min = ROBSTRIDE_O5_T_MIN,
    .t_max = ROBSTRIDE_O5_T_MAX
};

/**
 * @brief Robstride O6 motor limits
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:80-91]
 */
static const motor_limits_t robstride_o6_limits = {
    .p_min = ROBSTRIDE_O6_P_MIN,
    .p_max = ROBSTRIDE_O6_P_MAX,
    .v_min = ROBSTRIDE_O6_V_MIN,
    .v_max = ROBSTRIDE_O6_V_MAX,
    .kp_min = ROBSTRIDE_O6_KP_MIN,
    .kp_max = ROBSTRIDE_O6_KP_MAX,
    .kd_min = ROBSTRIDE_O6_KD_MIN,
    .kd_max = ROBSTRIDE_O6_KD_MAX,
    .t_min = ROBSTRIDE_O6_T_MIN,
    .t_max = ROBSTRIDE_O6_T_MAX
};

/*==============================================================================
 * MOTOR LOOKUP TABLE (O(1) Index Lookup)
 *============================================================================*/

/**
 * @brief Fast O(1) motor index lookup table
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:93-112]
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
 * NOTE: Non-static to allow inline function in header to access it.
 *       This table is copied to shared memory for Core 0 access.
 */
uint8_t g_motor_lookup[MOTOR_LOOKUP_ID_MAX][MOTOR_LOOKUP_BUS_MAX];

/*==============================================================================
 * MOTOR CONFIGURATION TABLE (23 DOF Robot)
 *============================================================================*/

/**
 * @brief Motor configuration table for 23 DOF humanoid robot
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:114-161]
 *
 * This table defines the complete motor mapping for the robot:
 * - Left Leg (6 motors): Hip (pitch, roll, yaw), Knee (pitch), Ankle (pitch, roll)
 * - Right Leg (6 motors): Hip (pitch, roll, yaw), Knee (pitch), Ankle (pitch, roll)
 * - Waist (1 motor): Yaw
 * - Left Arm (5 motors): Shoulder (pitch, roll, yaw), Elbow (pitch), Wrist (yaw)
 * - Right Arm (5 motors): Shoulder (pitch, roll, yaw), Elbow (pitch), Wrist (yaw)
 *
 * Each entry includes: CAN ID, bus number, motor type, direction, and limits pointer.
 */
static const motor_config_t motor_config_table[VD1_NUM_MOTORS] = {
    /* ===== LEFT LEG (Index 0-5) ===== */
        /* Hip (Pitch, Roll, Yaw) */
    {31, 5, MOTOR_TYPE_ROBSTRIDE_O4,  1.0f, robstride_o4_limits},  /* 0: Left Hip Pitch */
    {32, 5, MOTOR_TYPE_ROBSTRIDE_O3,  1.0f, robstride_o3_limits},  /* 1: Left Hip Roll */
    {33, 5, MOTOR_TYPE_ROBSTRIDE_O3,  1.0f, robstride_o3_limits},  /* 2: Left Hip Yaw */
    /* Knee (Pitch), Ankle (Pitch-Down, Roll-Up) */
    {34, 4, MOTOR_TYPE_ROBSTRIDE_O4,  1.0f, robstride_o4_limits},  /* 3: Left Knee Pitch */
    {36, 4, MOTOR_TYPE_ROBSTRIDE_O6,  1.0f, robstride_o6_limits},  /* 4: Left Ankle Pitch (Down) */
    {35, 4, MOTOR_TYPE_ROBSTRIDE_O6,  1.0f, robstride_o6_limits},  /* 5: Left Ankle Roll (Up) */

    /* ===== RIGHT LEG (Index 6-11) ===== */
    /* Hip (Pitch, Roll, Yaw) */
    {21, 2, MOTOR_TYPE_ROBSTRIDE_O4,  1.0f, robstride_o4_limits},  /* 6: Right Hip Pitch */
    {22, 2, MOTOR_TYPE_ROBSTRIDE_O3,  1.0f, robstride_o3_limits},  /* 7: Right Hip Roll */
    {23, 2, MOTOR_TYPE_ROBSTRIDE_O3,  1.0f, robstride_o3_limits},  /* 8: Right Hip Yaw */
    /* Knee (Pitch), Ankle (Pitch-Down, Roll-Up) */
    {24, 3, MOTOR_TYPE_ROBSTRIDE_O4,  1.0f, robstride_o4_limits},  /* 9: Right Knee Pitch */
    {26, 3, MOTOR_TYPE_ROBSTRIDE_O6,  1.0f, robstride_o6_limits},  /* 10: Right Ankle Pitch (Down) */
    {25, 3, MOTOR_TYPE_ROBSTRIDE_O6,  1.0f, robstride_o6_limits},  /* 11: Right Ankle Roll (Up) */

    /* ===== WAIST (Index 12) ===== */
    {11, 2, MOTOR_TYPE_ROBSTRIDE_O3,  1.0f, robstride_o3_limits},  /* 12: Waist Yaw */

    /* ===== LEFT ARM (Index 13-17) ===== */
    /* Shoulder (Pitch, Roll, Yaw) */
    {51, 6, MOTOR_TYPE_ROBSTRIDE_O3,  1.0f, robstride_o3_limits},  /* 13: Left Shoulder Pitch */
    {52, 6, MOTOR_TYPE_ROBSTRIDE_O2,  1.0f, robstride_o2_limits},  /* 14: Left Shoulder Roll */
    {53, 6, MOTOR_TYPE_ROBSTRIDE_O2,  1.0f, robstride_o2_limits},  /* 15: Left Shoulder Yaw */
    /* Elbow (Pitch), Wrist Yaw */
    {54, 7, MOTOR_TYPE_ROBSTRIDE_O2,  1.0f, robstride_o2_limits},  /* 16: Left Elbow Pitch */
    {56, 7, MOTOR_TYPE_ROBSTRIDE_O0,  1.0f, robstride_o0_limits},  /* 17: Left Wrist Yaw */

    /* ===== RIGHT ARM (Index 18-22) ===== */
    /* Shoulder (Pitch, Roll, Yaw) */
    {41, 1, MOTOR_TYPE_ROBSTRIDE_O3,  1.0f, robstride_o3_limits},  /* 18: Right Shoulder Pitch */
    {42, 1, MOTOR_TYPE_ROBSTRIDE_O2,  1.0f, robstride_o2_limits},  /* 19: Right Shoulder Roll */
    {43, 1, MOTOR_TYPE_ROBSTRIDE_O2,  1.0f, robstride_o2_limits},  /* 20: Right Shoulder Yaw */
    /* Elbow (Pitch), Wrist Yaw */
    {44, 0, MOTOR_TYPE_ROBSTRIDE_O2,  1.0f, robstride_o2_limits},  /* 21: Right Elbow Pitch */
    {46, 0, MOTOR_TYPE_ROBSTRIDE_O0,  1.0f, robstride_o0_limits},  /* 22: Right Wrist Yaw */
};

/*==============================================================================
 * PUBLIC FUNCTIONS
 *============================================================================*/

/**
 * @brief Get motor limits by type
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:167-185]
 *
 * @param type Motor type enumeration
 * @return Pointer to motor limits structure, or NULL if type unknown
 */
const motor_limits_t* motor_get_limits(motor_type_t type)
{
    switch (type) {
        case MOTOR_TYPE_ROBSTRIDE_O0:
            return &robstride_o0_limits;
        case MOTOR_TYPE_ROBSTRIDE_O2:
            return &robstride_o2_limits;
        case MOTOR_TYPE_ROBSTRIDE_O3:
            return &robstride_o3_limits;
        case MOTOR_TYPE_ROBSTRIDE_O4:
            return &robstride_o4_limits;
        case MOTOR_TYPE_ROBSTRIDE_O5:
            return &robstride_o5_limits;
        case MOTOR_TYPE_ROBSTRIDE_O6:
            return &robstride_o6_limits;
        default:
            return NULL;
    }
}

/**
 * @brief Get motor configuration by index
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:187-193]
 *
 * @param index Motor index (0-22)
 * @return Pointer to motor configuration structure, or NULL if index invalid
 */
const motor_config_t* motor_get_config(uint8_t index)
{
    if (index >= VD1_NUM_MOTORS) {
        return NULL;
    }
    return &motor_config_table[index];
}

/**
 * @brief Initialize motor mapping tables and populate shared memory
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:197-227]
 *
 * This function:
 * 1. Initializes the O(1) lookup table with 0xFF (not found)
 * 2. Builds the lookup table from motor_config_table
 * 3. Copies configuration data to shared memory for Core 0 access
 * 4. Verifies the lookup table is correctly built
 *
 * @note This is a CORE 1 ONLY function - must be called after shared memory init
 * @note Bare metal compliant: no dynamic allocation, no OS calls
 * @return 0 on success, -1 on error
 */
int motor_mapping_init_core1(void)
{
    /* Initialize lookup table with 0xFF (not found) */
    for (uint16_t mid = 0; mid < MOTOR_LOOKUP_ID_MAX; mid++) {
        for (uint8_t bus = 0; bus < MOTOR_LOOKUP_BUS_MAX; bus++) {
            g_motor_lookup[mid][bus] = 0xFF;
        }
    }

    /* Build lookup table from motor_config_table */
    for (uint8_t i = 0; i < VD1_NUM_MOTORS; i++) {
        uint8_t motor_id = motor_config_table[i].motor_id;
        uint8_t can_bus = motor_config_table[i].can_bus;

        if (motor_id < MOTOR_LOOKUP_ID_MAX && can_bus < MOTOR_LOOKUP_BUS_MAX) {
            g_motor_lookup[motor_id][can_bus] = i;
        }
    }

    /* Debug log to verify lookup table is built */
    uint8_t found_count = 0;
    for (uint8_t i = 0; i < VD1_NUM_MOTORS; i++) {
        uint8_t motor_id = motor_config_table[i].motor_id;
        uint8_t can_bus = motor_config_table[i].can_bus;
        uint8_t idx = g_motor_lookup[motor_id][can_bus];
        if (idx == i) {
            found_count++;
        }
    }
    (void)found_count;  /* Suppress unused warning */

    /* Copy configuration data to shared memory for Core 0 access */
    /* [MIGRATED TO gateway_shared.c:gateway_write_motor_config()] */
    extern int gateway_write_motor_config(const SharedMotorConfig_t* motor_config,
                                          const uint8_t motor_lookup[128][8]);

    /* Prepare shared memory configuration structures */
    SharedMotorConfig_t shared_config[VD1_NUM_MOTORS];

    for (uint8_t i = 0; i < VD1_NUM_MOTORS; i++) {
        shared_config[i].motor_id = motor_config_table[i].motor_id;
        shared_config[i].can_bus = motor_config_table[i].can_bus;
        shared_config[i].motor_type = (uint8_t)motor_config_table[i].motor_type;
        shared_config[i].reserved = 0;
        shared_config[i].direction = motor_config_table[i].direction;
        shared_config[i].p_min = motor_config_table[i].limits.p_min;
        shared_config[i].p_max = motor_config_table[i].limits.p_max;
        shared_config[i].v_min = motor_config_table[i].limits.v_min;
        shared_config[i].v_max = motor_config_table[i].limits.v_max;
        shared_config[i].kp_min = motor_config_table[i].limits.kp_min;
        shared_config[i].kp_max = motor_config_table[i].limits.kp_max;
        shared_config[i].kd_min = motor_config_table[i].limits.kd_min;
        shared_config[i].kd_max = motor_config_table[i].limits.kd_max;
        shared_config[i].t_min = motor_config_table[i].limits.t_min;
        shared_config[i].t_max = motor_config_table[i].limits.t_max;
    }

    /* Write to shared memory */
    int ret = gateway_write_motor_config(shared_config, g_motor_lookup);
    if (ret != 0) {
        return -1;  /* Failed to write to shared memory */
    }

    /* Signal Core 0 that motor configuration is ready */
    gateway_signal_motor_config_ready();
    DebugP_log("[Core1] Motor configuration signaled to Core 0\r\n");

    return 0;
}

/*==============================================================================
 * UTILITY FUNCTIONS
 *============================================================================*/

/**
 * @brief Convert uint16 to float with range
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:233-238]
 *
 * @param x Input uint16 value
 * @param x_min Minimum output float value
 * @param x_max Maximum output float value
 * @param bits Number of bits in input value
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
 * @param x Input float value
 * @param x_min Minimum input float value
 * @param x_max Maximum input float value
 * @param bits Number of bits in output value
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
 * @param bytedata Input byte array (must have at least 4 bytes)
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

/*==============================================================================
 * TABLE-DRIVEN CRC32 (Fast path for UDP TX at 1000Hz)
 *============================================================================*/

/**
 * @brief CRC32 lookup table (256 entries x 4 bytes = 1KB)
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:267-277]
 *
 * Pre-computed CRC values for all possible byte values (0-255).
 * Uses IEEE 802.3 polynomial: 0xEDB88320 (reversed)
 *
 * Performance: ~10-20us for 500 bytes vs ~200-400us for byte-by-byte
 * Speedup: ~10-20x faster
 *
 * Generated at compile time (const = stored in flash, not RAM)
 */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

/**
 * @brief Calculate CRC32 checksum using table-driven algorithm
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:324-350]
 *
 * Fast table-driven CRC32 implementation matching Python's binascii.crc32.
 * Uses IEEE 802.3 polynomial (0xEDB88320 reversed).
 *
 * Performance: ~10-20us for 500 bytes
 * Memory: 1KB lookup table (stored in flash)
 *
 * @param ptr Input data pointer
 * @param len Length in bytes
 * @return CRC32 value (no final XOR - matches xGW protocol)
 */
uint32_t crc32_core(const uint8_t* ptr, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;

    /* Table-driven lookup: one table access per byte instead of 8 bit operations */
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ ptr[i]) & 0xFF] ^ (crc >> 8);
    }

    /* Final XOR for CRC32-IEEE (CRC-32/ISO-HDLC) standard */
    crc ^= 0xFFFFFFFF;

    return crc;
}
