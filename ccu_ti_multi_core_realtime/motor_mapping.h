/**
 * @file motor_mapping.h
 * @brief Motor mapping configuration for ccu_ti xGW Gateway
 *
 * Contains mapping of 23 motors to CAN buses, motor IDs, types, and limits
 * Adapted from freertos_xgw for ccu_ti (TI AM263Px)
 *
 * @author Adapted from freertos_xgw by Chu Tien Thinh
 * @date 2025
 */

#ifndef MOTOR_MAPPING_H_
#define MOTOR_MAPPING_H_

#include <stdint.h>
#include <stdbool.h>
#include "can_interface.h"  /* For can_frame_t definition */

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define VD1_NUM_MOTORS            23
#define NUM_CAN_BUSES             8
#define DEFAULT_LOOP_FREQUENCY    1000  /* Hz - Target frequency for 1000Hz operation */

/* CAN Communication Types (Robstride Protocol) */
#define COMM_TYPE_GET_ID                  0x00
#define COMM_TYPE_MOTION_CONTROL          0x01
#define COMM_TYPE_MOTOR_REQUEST           0x02
#define COMM_TYPE_MOTOR_ENABLE            0x03
#define COMM_TYPE_MOTOR_STOP              0x04
#define COMM_TYPE_SET_POS_ZERO            0x06
#define COMM_TYPE_CAN_ID                  0x07
#define COMM_TYPE_CONTROL_MODE            0x12
#define COMM_TYPE_GET_SINGLE_PARAMETER    0x11
#define COMM_TYPE_SET_SINGLE_PARAMETER    0x12
#define COMM_TYPE_ERROR_FEEDBACK          0x15
#define COMM_TYPE_SAVE_PARAM              0x16
#define COMM_TYPE_ACTIVE_REPORT_FRAME     0x18

/* Master CAN ID */
#define MASTER_CAN_ID             0x00FD  /* 253 */

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

/**
 * @brief Motor type enumeration
 */
typedef enum {
    MOTOR_TYPE_ROBSTRIDE_O0 = 0,
    MOTOR_TYPE_ROBSTRIDE_O2 = 1,
    MOTOR_TYPE_ROBSTRIDE_O3 = 2,
    MOTOR_TYPE_ROBSTRIDE_O4 = 3,
    MOTOR_TYPE_ROBSTRIDE_O5 = 4,
    MOTOR_TYPE_ROBSTRIDE_O6 = 5,
    MOTOR_TYPE_UNKNOWN = 0xFF
} motor_type_t;

/**
 * @brief IMU type enumeration
 */
typedef enum {
    IMU_TYPE_YIS320 = 0,
    IMU_TYPE_OTHER = 1,
    IMU_TYPE_UNKNOWN = 0xFF
} imu_type_t;

/**
 * @brief Motor limits structure
 */
typedef struct {
    float p_min;     /* Position minimum (radians) */
    float p_max;     /* Position maximum (radians) */
    float v_min;     /* Velocity minimum (radians/sec) */
    float v_max;     /* Velocity maximum (radians/sec) */
    float kp_min;    /* Kp minimum */
    float kp_max;    /* Kp maximum */
    float kd_min;    /* Kd minimum */
    float kd_max;    /* Kd maximum */
    float t_min;     /* Torque minimum (Nm) */
    float t_max;     /* Torque maximum (Nm) */
} motor_limits_t;

/**
 * @brief Motor configuration structure
 */
typedef struct {
    uint8_t         motor_id;       /* CAN motor ID (1-127) */
    uint8_t         can_bus;        /* CAN bus number (0-7) */
    motor_type_t    motor_type;     /* Motor type */
    float           direction;      /* Direction multiplier (1.0 or -1.0) */
    motor_limits_t  limits;         /* Motor limits */
} motor_config_t;

/**
 * @brief Motor command structure
 */
typedef struct {
    uint8_t mode;       /* 1 = enable, 0 = disable */
    float   torque;     /* Torque feed-forward (Nm) */
    float   position;   /* Target position (rad) */
    float   velocity;   /* Target velocity (rad/s) */
    float   kp;         /* Position gain */
    float   kd;         /* Damping gain */
} motor_cmd_t;

/**
 * @brief Motor state structure
 */
typedef struct {
    float   position;     /* Current position (rad) */
    float   velocity;     /* Current velocity (rad/s) */
    float   torque;       /* Current torque (Nm) */
    float   temp;         /* Temperature (Celsius) */
    int8_t  pattern;      /* Motor pattern */
    uint8_t error_code;   /* Error code */
    uint8_t motor_id;     /* Motor ID */
    uint8_t can_bus;      /* CAN bus number */
    bool    updated;      /* Flag indicating new data received */
} motor_state_t;

/**
 * @brief IMU state structure
 */
typedef struct {
    float gyroscope[3];     /* Angular velocity [rad/s] - [x, y, z] */
    float rpy[3];           /* Roll, Pitch, Yaw [rad] */
    float quaternion[4];    /* Quaternion [w, x, y, z] */
    uint32_t timestamp;     /* Timestamp in milliseconds */
    bool    updated;        /* Flag indicating new data received */
} imu_state_t;

/*==============================================================================
 * MOTOR LIMITS (ROBSTRIDE)
 *============================================================================*/

/* Robstride O0 Limits (±14 Nm) ok*/
#define ROBSTRIDE_O0_P_MIN   -12.5f
#define ROBSTRIDE_O0_P_MAX    12.5f
#define ROBSTRIDE_O0_V_MIN   -33.0f
#define ROBSTRIDE_O0_V_MAX    33.0f
#define ROBSTRIDE_O0_KP_MIN    0.0f
#define ROBSTRIDE_O0_KP_MAX  500.0f
#define ROBSTRIDE_O0_KD_MIN    0.0f
#define ROBSTRIDE_O0_KD_MAX    5.0f
#define ROBSTRIDE_O0_T_MIN   -14.0f
#define ROBSTRIDE_O0_T_MAX    14.0f

/* Robstride O2 Limits (±17 Nm) ok*/
#define ROBSTRIDE_O2_P_MIN   -12.57f
#define ROBSTRIDE_O2_P_MAX    12.57f
#define ROBSTRIDE_O2_V_MIN   -44.0f
#define ROBSTRIDE_O2_V_MAX    44.0f
#define ROBSTRIDE_O2_KP_MIN    0.0f
#define ROBSTRIDE_O2_KP_MAX  500.0f
#define ROBSTRIDE_O2_KD_MIN    0.0f
#define ROBSTRIDE_O2_KD_MAX    5.0f
#define ROBSTRIDE_O2_T_MIN   -17.0f
#define ROBSTRIDE_O2_T_MAX    17.0f

/* Robstride O3 Limits (±60 Nm) */ //ok
#define ROBSTRIDE_O3_P_MIN   -12.57f
#define ROBSTRIDE_O3_P_MAX    12.57f
#define ROBSTRIDE_O3_V_MIN   -20.0f
#define ROBSTRIDE_O3_V_MAX    20.0f
#define ROBSTRIDE_O3_KP_MIN    0.0f
#define ROBSTRIDE_O3_KP_MAX 5000.0f
#define ROBSTRIDE_O3_KD_MIN    0.0f
#define ROBSTRIDE_O3_KD_MAX  100.0f
#define ROBSTRIDE_O3_T_MIN   -60.0f
#define ROBSTRIDE_O3_T_MAX    60.0f

/* Robstride O4 Limits (±120 Nm) ok*/
#define ROBSTRIDE_O4_P_MIN   -12.57f
#define ROBSTRIDE_O4_P_MAX    12.57f
#define ROBSTRIDE_O4_V_MIN   -15.0f
#define ROBSTRIDE_O4_V_MAX    15.0f
#define ROBSTRIDE_O4_KP_MIN    0.0f
#define ROBSTRIDE_O4_KP_MAX 5000.0f
#define ROBSTRIDE_O4_KD_MIN    0.0f
#define ROBSTRIDE_O4_KD_MAX  100.0f
#define ROBSTRIDE_O4_T_MIN  -120.0f
#define ROBSTRIDE_O4_T_MAX   120.0f

/* Robstride O5 Limits (±5.5 Nm) ok*/
#define ROBSTRIDE_O5_P_MIN   -12.57f
#define ROBSTRIDE_O5_P_MAX    12.57f
#define ROBSTRIDE_O5_V_MIN   -50.0f
#define ROBSTRIDE_O5_V_MAX    50.0f
#define ROBSTRIDE_O5_KP_MIN    0.0f
#define ROBSTRIDE_O5_KP_MAX  500.0f
#define ROBSTRIDE_O5_KD_MIN    0.0f
#define ROBSTRIDE_O5_KD_MAX    5.0f
#define ROBSTRIDE_O5_T_MIN    -5.5f
#define ROBSTRIDE_O5_T_MAX     5.5f

/* Robstride O6 Limits (±36 Nm) */
#define ROBSTRIDE_O6_P_MIN   -12.57f
#define ROBSTRIDE_O6_P_MAX    12.57f
#define ROBSTRIDE_O6_V_MIN   -50.0f
#define ROBSTRIDE_O6_V_MAX    50.0f
#define ROBSTRIDE_O6_KP_MIN    0.0f
#define ROBSTRIDE_O6_KP_MAX 5000.0f
#define ROBSTRIDE_O6_KD_MIN    0.0f
#define ROBSTRIDE_O6_KD_MAX  100.0f
#define ROBSTRIDE_O6_T_MIN   -36.0f
#define ROBSTRIDE_O6_T_MAX    36.0f

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * @brief Get motor limits by type
 * @param type Motor type
 * @return Pointer to motor limits structure
 */
const motor_limits_t* motor_get_limits(motor_type_t type);

/**
 * @brief Get motor configuration by index
 * @param index Motor index (0-22)
 * @return Pointer to motor configuration structure
 */
const motor_config_t* motor_get_config(uint8_t index);

/**
 * @brief Get motor index by CAN ID and bus (O(1) lookup table)
 * @param motor_id CAN motor ID
 * @param can_bus CAN bus number
 * @return Motor index (0-22) or 0xFF if not found
 *
 * NOTE: This is now O(1) using lookup table instead of O(n) linear search.
 * Critical for 1000Hz operation - called 23 times per cycle!
 *
 * Implementation is inline in header for maximum performance.
 */
static inline uint8_t motor_get_index(uint8_t motor_id, uint8_t can_bus)
{
    /* External reference to lookup table defined in motor_mapping.c */
    extern uint8_t g_motor_lookup[128][8];

    if (motor_id < 128 && can_bus < 8) {
        return g_motor_lookup[motor_id][can_bus];
    }
    return 0xFF;  /* Not found - invalid input */
}

/**
 * @brief Initialize motor mapping tables and lookup cache
 *
 * Builds the O(1) lookup table for fast motor_id+can_bus -> motor_idx mapping.
 * Must be called once at startup before any CAN operations.
 */
void motor_mapping_init(void);

/*==============================================================================
 * UTILITY FUNCTIONS
 *============================================================================*/

/**
 * @brief Convert uint16 to float with range
 * @param x Input value
 * @param x_min Minimum output
 * @param x_max Maximum output
 * @param bits Number of bits
 * @return Converted float value
 */
float uint16_to_float(uint16_t x, float x_min, float x_max, int bits);

/**
 * @brief Convert float to uint16 with range
 * @param x Input value
 * @param x_min Minimum input
 * @param x_max Maximum input
 * @param bits Number of bits
 * @return Converted uint16 value
 */
int float_to_uint(float x, float x_min, float x_max, int bits);

/**
 * @brief Convert 4 bytes to float (little-endian)
 * @param bytedata Input byte array
 * @return Float value
 */
float bytes_to_float(const uint8_t* bytedata);

/**
 * @brief Calculate CRC32 checksum using table-driven algorithm
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

#endif /* MOTOR_MAPPING_H_ */
