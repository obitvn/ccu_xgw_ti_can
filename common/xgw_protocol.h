/**
 * @file xgw_protocol.h
 * @brief xGW-Linux UDP Protocol Specification v2.0
 *
 * Protocol for real-time communication between xGW Gateway and Linux/ROS PC
 * Transport: Ethernet UDP
 * Endianness: Little Endian (ARM & x86 standard)
 * Alignment: 4-Byte (32-bit) Aligned
 *
 * Port Configuration:
 * - PC sends to: 61904 (0xF1D0)
 * - xGW sends to: 53489 (0xD0F1)
 *
 * @author Chu Tien Thinh
 * @date 2025-12-24
 * @version 2.0
 */

#ifndef XGW_PROTOCOL_H_
#define XGW_PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * PROTOCOL CONSTANTS
 *============================================================================*/

#define XGW_PROTOCOL_MAGIC        0x5A5A   /* Protocol signature */
#define XGW_PROTOCOL_VERSION      0x02     /* Protocol version */

/* UDP Port Configuration */
#define XGW_UDP_PORT_PC_TO_XGW    61904    /* 0xF1D0 - PC sends to xGW */
#define XGW_UDP_PORT_XGW_TO_PC    53489    /* 0xD0F1 - xGW sends to PC */

/* Message Types */
#define XGW_MSG_TYPE_MOTOR_STATE  0x01     /* xGW -> Linux: Motor feedback */
#define XGW_MSG_TYPE_MOTOR_CMD    0x02     /* Linux -> xGW: Motor command */
#define XGW_MSG_TYPE_IMU_STATE    0x03     /* xGW -> Linux: IMU data */
#define XGW_MSG_TYPE_RS485_DATA   0x04     /* Bidirectional: RS485 tunneling */
#define XGW_MSG_TYPE_XGW_DIAG     0x05     /* xGW -> Linux: Board diagnostics */
#define XGW_MSG_TYPE_XGW_CONFIG   0x06     /* Bidirectional: Configuration */
#define XGW_MSG_TYPE_MOTOR_SET    0x07     /* Linux -> xGW: Motor enable/disable */

/* Motor Set Operation Modes */
#define XGW_MOTOR_MODE_DISABLE    0x00
#define XGW_MOTOR_MODE_ENABLE     0x01
#define XGW_MOTOR_MODE_MECH_ZERO  0x02
#define XGW_MOTOR_MODE_ZERO_STA   0x03
#define XGW_MOTOR_MODE_ZERO_STA_MECH   0x04

/* Maximum sizes */
#define XGW_MAX_MOTORS            23       /* VD1 robot has 23 motors */
#define XGW_MAX_UDP_PAYLOAD_SIZE  1500     /* Standard MTU */

/*==============================================================================
 * COMMON HEADER (32 Bytes - Fixed for all packets)
 *============================================================================*/

/**
 * @brief Common packet header (32 bytes)
 *
 * All UDP packets start with this header followed by payload
 */
typedef struct __attribute__((packed)) {
    uint16_t magic;         /* 0x5A5A - Protocol signature */
    uint16_t header_meta;   /* User-defined flags or Sub-ID (default: 0) */
    uint8_t  version;       /* Protocol version (0x02) */
    uint8_t  msg_type;      /* Message type ID (0x01-0x07) */
    uint8_t  count;         /* Number of elements in payload */
    uint8_t  reserved;      /* Padding for alignment */
    uint32_t sequence;      /* Packet counter for loss detection */
    uint32_t payload_len;   /* Total size of payload in bytes */
    uint64_t timestamp_ns;  /* Monotonic timestamp (nanoseconds) */
    uint32_t crc32;         /* Checksum of Header + Payload */
    uint32_t reserved_pad;  /* Padding to reach 32 bytes */
} xgw_header_t;

/* Verify header size is 32 bytes */
_Static_assert(sizeof(xgw_header_t) == 32, "xgw_header_t must be 32 bytes");

/*==============================================================================
 * PAYLOAD STRUCTURES
 *============================================================================*/

/**
 * @brief Motor State payload element (20 bytes)
 * Type: 0x01 (MSG_TYPE_MOTOR_STATE)
 * Direction: xGW -> Linux
 */
typedef struct __attribute__((packed)) {
    uint8_t  motor_id;      /* Unique Motor ID */
    uint8_t  error_code;    /* Hardware Error Code (0 = OK) */
    int8_t   pattern;       /* Current Control Pattern/State */
    uint8_t  reserved;      /* Padding */
    float    position;      /* Shaft Position (rad) */
    float    velocity;      /* Shaft Velocity (rad/s) */
    float    torque;        /* Output Torque (Nm) */
    float    temp;          /* Motor/Driver Temperature (°C) */
} xgw_motor_state_t;

/* Verify motor state size is 20 bytes */
_Static_assert(sizeof(xgw_motor_state_t) == 20, "xgw_motor_state_t must be 20 bytes");

/**
 * @brief Motor Command payload element (24 bytes)
 * Type: 0x02 (MSG_TYPE_MOTOR_CMD)
 * Direction: Linux -> xGW
 */
typedef struct __attribute__((packed)) {
    uint8_t  motor_id;      /* Target Motor ID */
    uint8_t  mode;          /* Control mode (MIT=0, etc.) */
    uint8_t  reserved[2];   /* Padding */
    float    position;      /* Target Position (rad) */
    float    velocity;      /* Target Velocity (rad/s) */
    float    torque;        /* Feed-forward Torque (Nm) */
    float    kp;            /* P Gain */
    float    kd;            /* D Gain */
} xgw_motor_cmd_t;

/* Verify motor command size is 24 bytes */
_Static_assert(sizeof(xgw_motor_cmd_t) == 24, "xgw_motor_cmd_t must be 24 bytes");

/**
 * @brief IMU State payload (68 bytes)
 * Type: 0x03 (MSG_TYPE_IMU_STATE)
 * Direction: xGW -> Linux
 */
typedef struct __attribute__((packed)) {
    uint8_t  imu_id;        /* IMU Sensor ID */
    uint8_t  reserved;      /* Padding */
    int16_t  temp_cdeg;     /* Temperature (0.01°C) - 4500 = 45.00°C */
    float    gyro[3];       /* Angular velocity [rad/s] - [x, y, z] */
    float    quat[4];       /* Quaternion [w, x, y, z] */
    float    euler[3];      /* Euler angles [rad] - [roll, pitch, yaw] */
    float    mag_val[3];    /* Raw magnetic field [Gauss] */
    float    mag_norm[3];   /* Normalized magnetic vector */
} xgw_imu_state_t;

/* Verify IMU state size is 68 bytes */
_Static_assert(sizeof(xgw_imu_state_t) == 68, "xgw_imu_state_t must be 68 bytes");

/**
 * @brief RS485 Data payload header
 * Type: 0x04 (MSG_TYPE_RS485_DATA)
 * Direction: Bidirectional
 * Payload: Header (8 bytes) + Data Array (variable)
 */
typedef struct __attribute__((packed)) {
    uint8_t  port_id;       /* RS485 Port ID (0, 1...) */
    uint8_t  reserved[3];   /* Padding */
    uint32_t baud_rate;     /* Baudrate of this data chunk */
    /* Followed by uint8_t data[] */
} xgw_rs485_header_t;

/* Verify RS485 header size is 8 bytes */
_Static_assert(sizeof(xgw_rs485_header_t) == 8, "xgw_rs485_header_t must be 8 bytes");

/**
 * @brief xGW Diagnostics payload (32 bytes)
 * Type: 0x05 (MSG_TYPE_XGW_DIAG)
 * Direction: xGW -> Linux
 */
typedef struct __attribute__((packed)) {
    uint32_t uptime_ms;     /* Time since boot (ms) */
    uint16_t voltage_in;    /* Input Voltage (10mV) - 2400 = 24.0V */
    uint16_t current_in;    /* Input Current (10mA) - 100 = 1.0A */
    int16_t  temp_mcu;      /* MCU Core Temp (0.01°C) */
    int16_t  temp_pwr;      /* Power Stage Temp (0.01°C) */
    uint8_t  cpu_load;      /* CPU Usage (%) */
    uint8_t  ram_usage;     /* RAM Usage (%) */
    uint8_t  bus_load_can;  /* CAN Bus Load (%) */
    uint8_t  bus_load_485;  /* RS485 Bus Load (%) */
    uint32_t status_flags;  /* Status bitmask */
    uint8_t  reserved[12];  /* Padding to 32 bytes */
} xgw_diag_t;

/* Verify diagnostics size is 32 bytes */
_Static_assert(sizeof(xgw_diag_t) == 32, "xgw_diag_t must be 32 bytes");

/**
 * @brief xGW Configuration payload (48 bytes)
 * Type: 0x06 (MSG_TYPE_XGW_CONFIG)
 * Direction: Bidirectional
 *
 * Command IDs:
 * - 0: Get Config (Request)
 * - 1: Set Config (Apply temporarily)
 * - 2: Save Config (Write to Flash)
 * - 3: Reboot xGW
 */
typedef struct __attribute__((packed)) {
    uint8_t  cmd_id;        /* Command ID (0:GET, 1:SET, 2:SAVE, 3:BOOT) */
    uint8_t  reserved[3];   /* Padding */
    uint32_t can_baud;      /* CAN Bitrate (bps) */
    uint32_t rs485_baud;    /* RS485 Baudrate (bps) */
    uint32_t ip_addr;       /* IPv4 Address */
    uint32_t net_mask;      /* Subnet Mask */
    uint32_t gateway;       /* Gateway IP */
    uint16_t udp_port_host; /* Host (Linux) UDP Port */
    uint16_t udp_port_gw;   /* xGW UDP Port */
    uint32_t motor_map_id;  /* Motor Mapping Profile ID */
    uint8_t  reserved_block[16]; /* Future use */
} xgw_config_t;

/* Verify configuration size is 48 bytes */
_Static_assert(sizeof(xgw_config_t) == 48, "xgw_config_t must be 48 bytes");

/**
 * @brief Motor Set Operation payload (4 bytes)
 * Type: 0x07 (MSG_TYPE_MOTOR_SET)
 * Direction: Linux -> xGW
 */
typedef struct __attribute__((packed)) {
    uint8_t  motor_id;      /* Motor ID */
    uint8_t  mode;          /* 0: disable, 1: enable, 2: mech zero, 3: zero sta, 4: zero sta+mech */
    uint8_t  reserved[2];   /* Padding */
} xgw_motor_set_t;

/* Verify motor set size is 4 bytes */
_Static_assert(sizeof(xgw_motor_set_t) == 4, "xgw_motor_set_t must be 4 bytes");

/*==============================================================================
 * COMPLETE PACKET STRUCTURES
 *============================================================================*/

/**
 * @brief Complete Motor State packet
 * Size: 32 (header) + count * 20 (motor states)
 */
typedef struct {
    xgw_header_t     header;
    xgw_motor_state_t motors[XGW_MAX_MOTORS];
} xgw_motor_state_packet_t;

/**
 * @brief Complete Motor Command packet
 * Size: 32 (header) + count * 24 (motor commands)
 */
typedef struct {
    xgw_header_t      header;
    xgw_motor_cmd_t   motors[XGW_MAX_MOTORS];
} xgw_motor_cmd_packet_t;

/**
 * @brief Complete IMU State packet
 * Size: 32 (header) + 68 (IMU state) = 100 bytes
 */
typedef struct {
    xgw_header_t   header;
    xgw_imu_state_t imu;
} xgw_imu_state_packet_t;

/**
 * @brief Complete xGW Diagnostics packet
 * Size: 32 (header) + 32 (diag) = 64 bytes
 */
typedef struct {
    xgw_header_t header;
    xgw_diag_t    diag;
} xgw_diag_packet_t;

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * @brief Initialize protocol header
 * @param header Pointer to header structure
 * @param msg_type Message type
 * @param count Number of elements in payload
 * @param payload_len Payload length in bytes
 * @param sequence Packet sequence number
 */
void xgw_header_init(xgw_header_t* header, uint8_t msg_type, uint8_t count,
                     uint32_t payload_len, uint32_t sequence);

/**
 * @brief Calculate CRC32 for packet
 * @param header Packet header
 * @param payload Payload data
 * @param payload_len Payload length
 * @return CRC32 value
 */
uint32_t xgw_crc32_calculate(const xgw_header_t* header, const void* payload, uint32_t payload_len);

/**
 * @brief Validate packet CRC32
 * @param header Packet header
 * @param payload Payload data
 * @return true if CRC is valid, false otherwise
 */
bool xgw_crc32_validate(const xgw_header_t* header, const void* payload);

/**
 * @brief Get message type string (for debugging)
 * @param msg_type Message type
 * @return String representation
 */
const char* xgw_msg_type_to_string(uint8_t msg_type);

/**
 * @brief Print packet information (for debugging)
 * @param header Packet header
 */
void xgw_print_header(const xgw_header_t* header);

#ifdef __cplusplus
}
#endif

#endif /* XGW_PROTOCOL_H_ */
