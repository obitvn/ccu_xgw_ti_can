/**
 * @file motor_config_types.h
 * @brief Motor configuration type definitions for CCU Multicore Gateway
 *
 * Shared type definitions for motor configuration between Core 0 (FreeRTOS) and
 * Core 1 (NoRTOS). These types are used in shared memory for read-only access.
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:52-138]
 *
 * @author CCU Multicore Project
 * @date 2026-03-27
 */

#ifndef MOTOR_CONFIG_TYPES_H_
#define MOTOR_CONFIG_TYPES_H_

#include <stdint.h>
#include <stdbool.h>

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
 *
 * This structure defines the configuration for a single motor, including
 * its CAN ID, bus assignment, type, direction multiplier, and operational limits.
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
 *
 * Command structure for sending motor control commands via CAN
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
 *
 * State structure for receiving motor feedback from CAN
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
 *
 * State structure for IMU sensor data
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

/* Robstride O0 Limits (±14 Nm) */
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

/* Robstride O2 Limits (±17 Nm) */
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

/* Robstride O3 Limits (±60 Nm) */
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

/* Robstride O4 Limits (±120 Nm) */
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

/* Robstride O5 Limits (±5.5 Nm) */
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

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_CONFIG_TYPES_H_ */
