/**
 * @file motor_mapping.h
 * @brief Motor mapping for Core 1 (NoRTOS) - CCU Multicore
 *
 * @author CCU Multicore Project
 * @date 2026-03-18
 */

#ifndef MOTOR_MAPPING_H_
#define MOTOR_MAPPING_H_

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
#define DEFAULT_LOOP_FREQUENCY    1000

/* CAN Communication Types (Robstride Protocol) */
#define COMM_TYPE_GET_ID                  0x00
#define COMM_TYPE_MOTION_CONTROL          0x01
#define COMM_TYPE_MOTOR_REQUEST           0x02
#define COMM_TYPE_MOTOR_ENABLE            0x03
#define COMM_TYPE_MOTOR_STOP              0x04
#define COMM_TYPE_SET_POS_ZERO            0x06
#define COMM_TYPE_CAN_ID                  0x07
#define COMM_TYPE_CONTROL_MODE            0x12

/* Master CAN ID */
#define MASTER_CAN_ID             0x00FD

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

typedef enum {
    MOTOR_TYPE_ROBSTRIDE_O0 = 0,
    MOTOR_TYPE_ROBSTRIDE_O2 = 1,
    MOTOR_TYPE_ROBSTRIDE_O3 = 2,
    MOTOR_TYPE_ROBSTRIDE_O4 = 3,
    MOTOR_TYPE_ROBSTRIDE_O5 = 4,
    MOTOR_TYPE_ROBSTRIDE_O6 = 5,
    MOTOR_TYPE_UNKNOWN = 0xFF
} motor_type_t;

typedef struct {
    float p_min;
    float p_max;
    float v_min;
    float v_max;
    float kp_min;
    float kp_max;
    float kd_min;
    float kd_max;
    float t_min;
    float t_max;
} motor_limits_t;

typedef struct {
    uint8_t         motor_id;
    uint8_t         can_bus;
    motor_type_t    motor_type;
    float           direction;
    motor_limits_t  limits;
} motor_config_t;

typedef struct {
    uint8_t mode;
    float   torque;
    float   position;
    float   velocity;
    float   kp;
    float   kd;
} motor_cmd_t;

typedef struct {
    float   position;
    float   velocity;
    float   torque;
    float   temp;
    int8_t  pattern;
    uint8_t error_code;
    uint8_t motor_id;
    uint8_t can_bus;
    bool    updated;
} motor_state_t;

/*==============================================================================
 * MOTOR LIMITS (ROBSTRIDE)
 *============================================================================*/

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

const motor_limits_t* motor_get_limits(motor_type_t type);
const motor_config_t* motor_get_config(uint8_t index);

static inline uint8_t motor_get_index(uint8_t motor_id, uint8_t can_bus)
{
    extern uint8_t g_motor_lookup[128][8];

    if (motor_id < 128 && can_bus < 8) {
        return g_motor_lookup[motor_id][can_bus];
    }
    return 0xFF;
}

void motor_mapping_init(void);

float uint16_to_float(uint16_t x, float x_min, float x_max, int bits);
int float_to_uint(float x, float x_min, float x_max, int bits);
float bytes_to_float(const uint8_t* bytedata);
uint32_t crc32_core(const uint8_t* ptr, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_MAPPING_H_ */
