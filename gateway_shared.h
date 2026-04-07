/**
 * @file gateway_shared.h
 * @brief Shared Memory IPC for CCU Multicore Gateway
 *
 * Lock-free shared memory communication between R5F0-0 (FreeRTOS) and R5F0-1 (NoRTOS)
 * Memory Region: 0x701D0000 (USER_SHM_MEM) - 16KB Non-cacheable
 *
 * Data Flow:
 * - Core 0 (FreeRTOS): Ethernet <-> Shared Memory (Consumer/Producer)
 * - Core 1 (NoRTOS): 8x CAN <-> Shared Memory (Producer/Consumer)
 *
 * @author CCU Multicore Project
 * @date 2026-03-18
 */

#ifndef GATEWAY_SHARED_H_
#define GATEWAY_SHARED_H_

#include <stdint.h>
#include <stdbool.h>
#include "common/motor_config_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * PLATFORM-SPECIFIC DEFINITIONS
 *============================================================================*/

/* Cache Line Size for Cortex-R5F (32 bytes) */
#define CACHE_LINE_SIZE         32U

/* Memory Barriers for ARM Cortex-R5F */
/* Using inline assembly for tiarmclang compatibility */
#define MEMORY_BARRIER_DMB()    __asm__ volatile("dmb 15" ::: "memory")   /* Data Memory Barrier */
#define MEMORY_BARRIER_DSB()    __asm__ volatile("dsb 15" ::: "memory")   /* Data Synchronization Barrier */
#define MEMORY_BARRIER_FULL()   do { __asm__ volatile("dmb 15" ::: "memory"); \
                                     __asm__ volatile("dsb 15" ::: "memory"); } while(0)

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define GATEWAY_SHARED_MAGIC        0x47444154  /* "GTAD" */
#define GATEWAY_SHARED_VERSION      0x00040000  /* Version 4.0.0 - Lock-free Ring Buffer */

/* Shared Memory Configuration */
#define GATEWAY_NUM_MOTORS          23      /* Number of motors in VD1 robot */
#define GATEWAY_NUM_CAN_BUSES       8
#define GATEWAY_SHARED_MEM_ADDR     0x70200000  /* [FIX B076] Match linker USER_SHM_MEM */
#define GATEWAY_SHARED_MEM_SIZE     0x8000      /* 32KB - matches USER_SHM_MEM LENGTH */

/* Lock-free Ring Buffer Configuration */
#define GATEWAY_USE_LOCKFREE_RINGBUF    1   /* Enable lock-free ring buffer */

/* Ring Buffer Sizes (power of 2 for efficient modulo) */
#define GATEWAY_RINGBUF_0_TO_1_SIZE     (16 * 1024)  /* 16KB: Core0 -> Core1 (power of 2) [FIX B076] Increased from 8KB */
#define GATEWAY_RINGBUF_1_TO_0_SIZE     (8 * 1024)    /* 8KB: Core1 -> Core0 (power of 2) */
#define GATEWAY_RINGBUF_MAX_ITEM_SIZE   256          /* Max packet size */

/* Verify power of 2 */
#if (GATEWAY_RINGBUF_0_TO_1_SIZE & (GATEWAY_RINGBUF_0_TO_1_SIZE - 1)) != 0
#error "GATEWAY_RINGBUF_0_TO_1_SIZE must be power of 2"
#endif
#if (GATEWAY_RINGBUF_1_TO_0_SIZE & (GATEWAY_RINGBUF_1_TO_0_SIZE - 1)) != 0
#error "GATEWAY_RINGBUF_1_TO_0_SIZE must be power of 2"
#endif

#define GATEWAY_RINGBUF_0_TO_1_MASK     (GATEWAY_RINGBUF_0_TO_1_SIZE - 1)
#define GATEWAY_RINGBUF_1_TO_0_MASK     (GATEWAY_RINGBUF_1_TO_0_SIZE - 1)

/* IPC Message IDs (IpcNotify) */
#define MSG_CAN_DATA_READY          0x01    /* Core 1 -> Core 0: Motor states available */
#define MSG_ETH_DATA_READY          0x02    /* Core 0 -> Core 1: Motor commands available */
#define MSG_CAN_DATA_ACK            0x03    /* Core 0 -> Core 1: Ack states received */
#define MSG_ETH_DATA_ACK            0x04    /* Core 1 -> Core 0: Ack commands received */
#define MSG_RS485_DATA_READY        0x05    /* Future: RS485 data */
#define MSG_IMU_DATA_READY          0x06    /* Future: IMU data */
#define MSG_HEARTBEAT               0x07    /* Heartbeat sync */
#define MSG_EMERGENCY_STOP          0x08    /* Emergency stop signal */

/* Feature flag: Enable Ping-Pong Buffer (set to 1 to enable) */
#define GATEWAY_USE_PINGPONG_BUFFER  0

/* IPC Client ID - BOTH cores must register the SAME client ID for IPC communication */
#define GATEWAY_IPC_CLIENT_ID        4U     /* Common client ID for both cores */

/*==============================================================================
 * LOCK-FREE RING BUFFER TYPE DEFINITIONS
 *============================================================================*/

/**
 * @brief Lock-free Ring Buffer Error Codes
 */
typedef enum {
    GATEWAY_RINGBUF_OK              = 0,   /* Operation successful */
    GATEWAY_RINGBUF_FULL            = -1,  /* Producer: No space available */
    GATEWAY_RINGBUF_EMPTY           = -2,  /* Consumer: No data available */
    GATEWAY_RINGBUF_INVALID_PARAM   = -3,  /* Invalid parameter */
    GATEWAY_RINGBUF_PARTIAL_WRITE   = -4,  /* Partial write (chunk too large) */
} Gateway_RingBuf_Result_t;

/**
 * @brief Lock-free Ring Buffer Control Header
 *
 * Each control variable is aligned to its own cache line (32 bytes)
 * to prevent false sharing between Producer and Consumer.
 *
 * Access Rights:
 * - Producer: Only writes write_index, reads read_index
 * - Consumer: Only writes read_index, reads write_index
 */
typedef struct {
    /* === Producer State (Write-Only by Producer, Read-Only by Consumer) === */
    volatile uint32_t write_index __attribute__((aligned(CACHE_LINE_SIZE)));
    uint8_t _padding_write[CACHE_LINE_SIZE - sizeof(uint32_t)];

    /* Reserved for future use */
    uint32_t _reserved1[(CACHE_LINE_SIZE / sizeof(uint32_t))];

    /* === Consumer State (Write-Only by Consumer, Read-Only by Producer) === */
    volatile uint32_t read_index __attribute__((aligned(CACHE_LINE_SIZE)));
    uint8_t _padding_read[CACHE_LINE_SIZE - sizeof(uint32_t)];

    /* Reserved for future use */
    uint32_t _reserved2[(CACHE_LINE_SIZE / sizeof(uint32_t))];

} Gateway_RingBuf_Control_t;

/* Verify alignment */
_Static_assert(sizeof(Gateway_RingBuf_Control_t) == (4 * CACHE_LINE_SIZE),
               "Gateway_RingBuf_Control_t size must be 4 cache lines");

/**
 * @brief Lock-free Ring Buffer
 *
 * Total size: Control header (128 bytes) + Data buffer
 */
typedef struct {
    Gateway_RingBuf_Control_t ctrl;           /* Cache-aligned control variables */
    uint32_t size;                            /* Buffer size (power of 2) */
    uint32_t mask;                            /* Size mask for modulo */
    uint8_t data[1];                          /* Flexible array member */
} Gateway_RingBuf_t;

/* Legacy definitions (kept for compatibility but not used for IPC Notify) */
#define CLIENT_ID_CAN_RX            0       /* Core 1: CAN RX - DEPRECATED */
#define CLIENT_ID_ETH_TX            1       /* Core 0: Ethernet TX - DEPRECATED */
#define CLIENT_ID_CAN_TX            2       /* Core 1: CAN TX - DEPRECATED */
#define CLIENT_ID_ETH_RX            3       /* Core 0: Ethernet RX - DEPRECATED */

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

/**
 * @brief Motor Command Modes
 *
 * These modes match xGW protocol motor_set modes (XGW_MOTOR_MODE_* in xgw_protocol.h)
 *
 * Modes 0-4: Motor set commands (ONE-SHOT - sent immediately, NOT stored cyclically)
 *           After sending, command is cleared/reset
 *           Mode 0 (disable) triggers reset of ALL motors to default MIT values
 *
 * MIT Motion Control: When mode field is not 0-4, treat as MIT motion control (CYCLIC)
 *                    MIT commands are stored in working buffer and sent at 1000Hz
 *                    Recommended: Set mode=255 for MIT motion control
 */
#define MOTOR_MODE_DISABLE             0   /* Disable motor - one-shot, resets ALL motors to default */
#define MOTOR_MODE_ENABLE              1   /* Enable motor - one-shot */
#define MOTOR_MODE_MECH_ZERO           2   /* Mechanical zero - one-shot */
#define MOTOR_MODE_ZERO_STA            3   /* Zero STA - one-shot */
#define MOTOR_MODE_ZERO_STA_MECH       4   /* Zero STA+Mech - one-shot */

/* Use mode=255 for MIT motion control (cyclic) to distinguish from motor_set commands */
#define MOTOR_MODE_MIT_CONTROL         255  /* MIT motion control - cyclic (default) */

/**
 * @brief Motor command for IPC (Ethernet -> CAN)
 *
 * Optimized packed structure for shared memory transfer
 *
 * Mode field behavior:
 * - 0-1: Motion control commands (MIT mode, enable)
 *        -> Stored in working buffer, sent cyclically at 1000Hz
 *        -> Mode 0 (disable) triggers reset to default command
 * - 2-4: Motor set commands (mech zero, zero sta, zero sta+mech)
 *        -> Sent immediately as one-shot CAN frames
 *        -> NOT stored in buffer (don't repeat)
 */
typedef struct __attribute__((packed)) {
    uint8_t  motor_id;      /* Motor ID (1-127) */
    uint8_t  can_bus;       /* CAN bus (0-7) */
    uint8_t  mode;          /* Mode: 0=MIT, 1=Enable, 2=MechZero, 3=ZeroSTA, 4=ZeroSTA+Mech */
    uint8_t  reserved;
    float    position;      /* Position (rad) - [FIX B038] Changed from uint16 to float */
    float    velocity;      /* Velocity (rad/s) - [FIX B038] Changed from int16 to float */
    float    torque;        /* Torque (Nm) - [FIX B038] Changed from int16 to float */
    float    kp;            /* Kp gain - [FIX B038] Changed from uint16 to float */
    float    kd;            /* Kd gain - [FIX B038] Changed from uint16 to float */
} motor_cmd_ipc_t;

/**
 * @brief Motor state for IPC (CAN -> Ethernet)
 *
 * Optimized packed structure for shared memory transfer
 * [FIX B064] Changed to float to avoid conversion errors
 * [FIX B066] motor_id is now array index (0-22), not hardware CAN ID
 */
typedef struct __attribute__((packed)) {
    uint8_t  motor_id;      /* Array Index (0-22) - [FIX B066] Changed from hardware ID */
    uint8_t  can_bus;       /* CAN bus (0-7) */
    int8_t   pattern;       /* Control pattern */
    uint8_t  error_code;    /* Error code */
    float    position;      /* Position (rad) - [FIX B064] Changed from int16 to float */
    float    velocity;      /* Velocity (rad/s) - [FIX B064] Changed from int16 to float */
    float    torque;        /* Torque (Nm) - [FIX B064] Changed from int16 to float */
    float    temperature;   /* Temperature (°C) - [FIX B064] Changed from int16 to float */
} motor_state_ipc_t;

/**
 * @brief IMU state for IPC (Core 1 -> Core 0)
 *
 * Optimized packed structure for shared memory transfer
 * Based on xGW IMU state structure
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
} imu_state_ipc_t;

/**
 * @brief Shared motor configuration for read-only access (Core 1 -> Core 0)
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.h:93-100]
 *
 * This structure provides read-only access to motor configuration data
 * maintained by Core 1. Core 0 can query this data via shared memory
 * without needing to access Core 1's local memory directly.
 *
 * Access Rights:
 * - Core 1 (NoRTOS): Writes to shared memory at initialization
 * - Core 0 (FreeRTOS): Read-only access for queries
 *
 * @note This is a read-only snapshot - Core 0 cannot modify configuration
 * @note Configuration is static after Core 1 initialization
 */
typedef struct __attribute__((packed)) {
    uint8_t  motor_id;       /* CAN motor ID (1-127) */
    uint8_t  can_bus;        /* CAN bus number (0-7) */
    uint8_t  motor_type;     /* Motor type (motor_type_t) */
    uint8_t  reserved;       /* Padding/Reserved */
    float    direction;      /* Direction multiplier (1.0 or -1.0) */
    /* Motor limits embedded directly for read-only access */
    float    p_min;          /* Position minimum (radians) */
    float    p_max;          /* Position maximum (radians) */
    float    v_min;          /* Velocity minimum (radians/sec) */
    float    v_max;          /* Velocity maximum (radians/sec) */
    float    kp_min;         /* Kp minimum */
    float    kp_max;         /* Kp maximum */
    float    kd_min;         /* Kd minimum */
    float    kd_max;         /* Kd maximum */
    float    t_min;          /* Torque minimum (Nm) */
    float    t_max;          /* Torque maximum (Nm) */
} SharedMotorConfig_t;

/**
 * @brief Double buffered motor state buffer
 *
 * Core 1 writes, Core 0 reads
 * Double buffering prevents race conditions
 */
typedef struct {
    motor_state_ipc_t motors[GATEWAY_NUM_MOTORS];  /* 23 motor states */
    volatile uint32_t ready_flag;                   /* Data ready flag */
    volatile uint32_t sequence;                     /* Sequence number */
    uint32_t write_idx;                             /* Current write buffer index */
    uint32_t reserved;
} can_to_eth_buf_t;

/**
 * @brief Motor command buffer
 *
 * Core 0 writes, Core 1 reads
 */
typedef struct {
    motor_cmd_ipc_t motors[GATEWAY_NUM_MOTORS];    /* 23 motor commands */
    volatile uint32_t ready_flag;                   /* Data ready flag */
    volatile uint32_t sequence;                     /* Sequence number */
    uint32_t reserved;
} eth_to_can_buf_t;

#if GATEWAY_USE_PINGPONG_BUFFER
/*==============================================================================
 * PING-PONG BUFFER STRUCTURES (Experimental)
 *============================================================================*/

/**
 * @brief Ping-pong buffer statistics
 */
typedef struct {
    uint32_t write_count;
    uint32_t read_count;
    uint32_t buffer_switch_count;
    uint32_t seq_error_count;
    uint32_t timeout_count;
    uint32_t last_seq;
    uint32_t reserved[8];
} ppbuf_stats_t;

/**
 * @brief Ping-pong buffer for CAN -> Ethernet
 */
typedef struct {
    motor_state_ipc_t buffer[2][GATEWAY_NUM_MOTORS];
    volatile uint32_t write_idx;
    volatile uint32_t read_idx;
    volatile uint32_t write_seq[2];
    volatile uint32_t write_done[2];
    ppbuf_stats_t stats;
} can_to_eth_ppbuf_t;

/**
 * @brief Ping-pong buffer for Ethernet -> CAN
 */
typedef struct {
    motor_cmd_ipc_t buffer[2][GATEWAY_NUM_MOTORS];
    volatile uint32_t write_idx;
    volatile uint32_t read_idx;
    volatile uint32_t write_seq[2];
    volatile uint32_t write_done[2];
    ppbuf_stats_t stats;
} eth_to_can_ppbuf_t;

/* IPC Client ID for ping-pong mode */
#define GATEWAY_IPC_CLIENT_ID        4U

#endif /* GATEWAY_USE_PINGPONG_BUFFER */

/**
 * @brief Statistics section
 */
typedef struct {
    uint32_t can_rx_count;
    uint32_t can_tx_count;
    uint32_t udp_rx_count;
    uint32_t udp_tx_count;
    uint32_t ipc_notify_count[4];
    uint32_t error_count;
    uint32_t reserved[8];
} gateway_stats_t;

/**
 * @brief Diagnostics section
 */
typedef struct {
    uint8_t  can_bus_off[GATEWAY_NUM_CAN_BUSES];   /* Bus-off flags */
    uint8_t  can_error_count[GATEWAY_NUM_CAN_BUSES]; /* Error counters */
    uint16_t cpu_load[2];                          /* CPU load % [Core0, Core1] */
    uint32_t uptime_ms;
    uint32_t reserved[11];
} gateway_diag_t;

/**
 * @brief Gateway Shared Memory Structure
 *
 * Total size: ~28KB (lock-free ring buffers + legacy buffers)
 * Placed at 0x701D0000 with non-cacheable MPU settings
 */
typedef struct __attribute__((packed)) {
    /* === Header Section (128 bytes) === */
    uint32_t magic;                                /* 0x47444154 ("GTAD") */
    uint32_t version;                              /* Structure version */
    volatile uint32_t heartbeat_r5f0_0;            /* Core 0 heartbeat counter */
    volatile uint32_t heartbeat_r5f0_1;            /* Core 1 heartbeat counter */
    volatile uint32_t emergency_stop_flag;         /* Emergency stop signal (Core1->Core0) */
    uint32_t reserved[27];                         /* Alignment padding */

#if GATEWAY_USE_LOCKFREE_RINGBUF
    /* === Lock-free Ring Buffers === */
    /* Buffer 0: Core0 (Producer) -> Core1 (Consumer) */
    uint8_t ringbuf_0_to_1[sizeof(Gateway_RingBuf_Control_t) + GATEWAY_RINGBUF_0_TO_1_SIZE];

    /* Buffer 1: Core1 (Producer) -> Core0 (Consumer) */
    uint8_t ringbuf_1_to_0[sizeof(Gateway_RingBuf_Control_t) + GATEWAY_RINGBUF_1_TO_0_SIZE];
#endif

    /* === Read-Only Motor Configuration (Core 1 -> Core 0) === */
    /* [MIGRATED FROM draft/ccu_ti/motor_mapping.c:114-160] */
    SharedMotorConfig_t motor_config[VD1_NUM_MOTORS];  /* 23 motor configurations */
    volatile uint32_t motor_config_ready;              /* Configuration ready flag */

    /* === Motor Lookup Table (O(1) Index Lookup) === */
    /* [MIGRATED FROM draft/ccu_ti/motor_mapping.c:93-112] */
    /* Maps (motor_id, can_bus) -> motor_idx for constant-time lookup */
    uint8_t motor_lookup[128][8];                      /* 128 motor IDs x 8 buses = 1024 bytes */

    uint32_t motor_config_reserved[2];                 /* Padding */

    /* === Legacy Buffers (for backward compatibility) === */
#if GATEWAY_USE_PINGPONG_BUFFER
    /* === Ping-Pong Buffers === */
    can_to_eth_ppbuf_t can_to_eth_ppbuf;
    eth_to_can_ppbuf_t eth_to_can_ppbuf;
#else
    /* === Single Buffers === */
    can_to_eth_buf_t can_to_eth_buf;
    eth_to_can_buf_t eth_to_can_buf;
#endif

    /* === IMU Buffer (Core 1 -> Core 0) === */
    imu_state_ipc_t imu_state;
    volatile uint32_t imu_ready_flag;               /* IMU data ready flag */
    volatile uint32_t imu_sequence;                 /* IMU sequence number */
    uint32_t imu_reserved[3];                       /* Padding */

    /* === Statistics Section === */
    gateway_stats_t stats;

    /* === Diagnostics Section === */
    gateway_diag_t diag;

} GatewaySharedData_t;

/* Verify structure size */
_Static_assert(sizeof(GatewaySharedData_t) < GATEWAY_SHARED_MEM_SIZE,
               "GatewaySharedData_t exceeds shared memory size");

/*==============================================================================
 * SHARED MEMORY INSTANCE
 *============================================================================*/

/**
 * @brief Shared memory instance
 *
 * Placed in .bss.user_shared_mem section (0x701D0000)
 * Must be defined as non-cacheable in MPU configuration
 */
extern volatile GatewaySharedData_t gGatewaySharedMem;

/*==============================================================================
 * PUBLIC API - Core 0 (FreeRTOS, Ethernet)
 *============================================================================*/

/**
 * @brief Initialize Core 0 shared memory access
 *
 * @return 0 on success, -1 on error
 */
int gateway_core0_init(void);

/**
 * @brief Finalize Core 0 initialization (call after sync)
 *
 * @return 0 on success, -1 on error
 */
int gateway_core0_finalize(void);

/**
 * @brief Read motor commands from shared memory (Core 0 -> Core 1)
 *
 * @param cmds Output buffer (23 motors)
 * @return Number of commands read
 */
int gateway_read_motor_commands(motor_cmd_ipc_t* cmds);

/**
 * @brief Write motor states to UDP packet buffer (Core 0 reads from Core 1)
 *
 * @param states Output buffer (23 motors)
 * @return Number of states read
 */
int gateway_read_motor_states(motor_state_ipc_t* states);

/**
 * @brief IPC notification callback for Core 0
 *
 * @param clientId Client ID
 * @param msg Message ID
 */
void gateway_core0_ipc_callback(uint16_t clientId, uint16_t msg);

/*==============================================================================
 * PUBLIC API - Core 1 (NoRTOS, CAN)
 *============================================================================*/

/**
 * @brief Initialize Core 1 shared memory access
 *
 * @return 0 on success, -1 on error
 */
int gateway_core1_init(void);

/**
 * @brief Wait for Core 0 to initialize shared memory (call after sync)
 *
 * @return 0 on success, -1 on error
 */
int gateway_core1_wait_for_ready(void);

/**
 * @brief Write motor commands from shared memory (Core 1 reads from Core 0)
 *
 * @param cmds Output buffer (23 motors)
 * @return Number of commands read
 */
int gateway_read_motor_commands_core1(motor_cmd_ipc_t* cmds);

/**
 * @brief Write motor states to shared memory (Core 1 -> Core 0)
 *
 * @param states Motor states (23 motors)
 * @param count Number of states
 * @return 0 on success, -1 on error
 */
int gateway_write_motor_states(const motor_state_ipc_t* states, uint16_t count);

/**
 * @brief IPC notification callback for Core 1
 *
 * @param clientId Client ID
 * @param msg Message ID
 */
void gateway_core1_ipc_callback(uint16_t clientId, uint16_t msg);

/*==============================================================================
 * SHARED UTILITIES
 *============================================================================*/

/**
 * @brief Update heartbeat counter
 *
 * @param core_id Core ID (0 or 1)
 */
void gateway_update_heartbeat(uint8_t core_id);

/**
 * @brief Get heartbeat counter
 *
 * @param core_id Core ID (0 or 1)
 * @return Heartbeat counter value
 */
uint32_t gateway_get_heartbeat(uint8_t core_id);

/**
 * @brief Check if both cores are alive
 *
 * @return true if both hearts beating, false otherwise
 */
bool gateway_check_heartbeat(void);

/**
 * @brief Check emergency stop flag
 *
 * @return 1 if emergency stop is active, 0 otherwise
 */
int gateway_check_emergency_stop(void);

/**
 * @brief Set emergency stop flag (trigger emergency stop)
 *
 * Sets the shared emergency_stop_flag to notify both cores.
 * Called when emergency stop condition is detected.
 */
void gateway_set_emergency_stop(void);

/**
 * @brief Clear emergency stop flag
 *
 * Should be called after handling the emergency stop condition
 */
void gateway_clear_emergency_stop(void);

/**
 * @brief Update statistics
 *
 * @param core_id Core ID (0 or 1)
 * @param stat_id Stat ID to increment
 */
void gateway_update_stat(uint8_t core_id, uint8_t stat_id);

/**
 * @brief Notify other core that data is ready
 */
int gateway_notify_commands_ready(void);
int gateway_notify_states_ready(void);

/**
 * @brief Write IMU state to shared memory (Core 1 -> Core 0)
 *
 * @param imu_state IMU state data
 * @return 0 on success, -1 on error
 */
int gateway_write_imu_state(const imu_state_ipc_t* imu_state);

/**
 * @brief Read IMU state from shared memory (Core 0 reads from Core 1)
 *
 * @param imu_state Output IMU state buffer
 * @return 0 on success, -1 on error
 */
int gateway_read_imu_state(imu_state_ipc_t* imu_state);

/**
 * @brief Notify that IMU data is ready (Core 1 -> Core 0)
 *
 * @return 0 on success, -1 on error
 */
int gateway_notify_imu_ready(void);

/*==============================================================================
 * EMERGENCY STOP API
 *============================================================================*/

/**
 * @brief Core 0: Emergency stop handler
 *
 * Called when emergency stop is triggered (from Core 1 via IPC or locally).
 * Implements basic emergency stop actions:
 * - Log event to syslog/UART
 * - Set shared memory emergency flag
 * - Notify Core 1 via IPC to stop all motors
 * - Optional: Set emergency GPIO output (if configured)
 *
 * @note This is a CORE 0 ONLY function
 * @note Emergency GPIO is optional - requires CONFIG_GPIO_EMERGENCY_PIN to be defined
 * @note Motor shutdown is handled by Core 1 via IPC notification
 *
 * [STUB S006] - Implemented basic emergency stop handler
 */
void gateway_core0_emergency_stop_handler(void);

/*==============================================================================
 * SHARED MOTOR CONFIGURATION API
 *============================================================================*/

/**
 * @brief Core 1: Write motor configuration to shared memory
 *
 * [MIGRATED FROM draft/ccu_ti/motor_mapping.c:197-227]
 *
 * Core 1 calls this function to populate the shared memory with
 * motor configuration data after initializing its local tables.
 *
 * @param motor_config Local motor configuration table (23 motors)
 * @param motor_lookup Local motor lookup table (128x8)
 * @return 0 on success, -1 on error
 *
 * @note This is a CORE 1 ONLY function
 * @note Must be called after motor_mapping_init_core1()
 */
int gateway_write_motor_config(const SharedMotorConfig_t* motor_config,
                                const uint8_t motor_lookup[128][8]);

/**
 * @brief Core 0: Read motor configuration from shared memory
 *
 * Core 0 calls this function to access motor configuration data
 * maintained by Core 1.
 *
 * @param index Motor index (0-22)
 * @return Pointer to motor configuration in shared memory, or NULL if invalid
 *
 * @note This is a CORE 0 ONLY function - read-only access
 * @note Returns pointer to shared memory - do not modify!
 */
const SharedMotorConfig_t* gateway_get_motor_config(uint8_t index);

/**
 * @brief Core 0: Get motor index by CAN ID and bus from shared memory
 *
 * O(1) lookup using the shared memory lookup table maintained by Core 1.
 *
 * @param motor_id CAN motor ID
 * @param can_bus CAN bus number
 * @return Motor index (0-22) or 0xFF if not found
 *
 * @note This is a CORE 0 ONLY function - read-only access
 * @note Critical for 1000Hz operation
 */
static inline uint8_t gateway_get_motor_index(uint8_t motor_id, uint8_t can_bus)
{
    extern volatile GatewaySharedData_t gGatewaySharedMem;

    if (motor_id < 128 && can_bus < 8) {
        return gGatewaySharedMem.motor_lookup[motor_id][can_bus];
    }
    return 0xFF;  /* Not found - invalid input */
}

/**
 * @brief Wait for motor configuration to be ready (Core 0)
 *
 * Core 0 calls this to wait for Core 1 to finish writing configuration.
 *
 * @param timeout_ms Timeout in milliseconds (0 = wait forever)
 * @return 0 on success, -1 on timeout
 *
 * @note This is a CORE 0 ONLY function
 */
int gateway_wait_motor_config_ready(uint32_t timeout_ms);

#if GATEWAY_USE_LOCKFREE_RINGBUF
/*==============================================================================
 * LOCK-FREE RING BUFFER API
 *============================================================================*/

/**
 * @brief Initialize lock-free ring buffer
 *
 * @param ringbuf Pointer to ring buffer
 * @param size Buffer size (must be power of 2)
 * @return 0 on success, -1 on error
 */
int gateway_ringbuf_init(void* ringbuf, uint32_t size);

/**
 * @brief Reset ring buffer indices
 *
 * @param ringbuf Pointer to ring buffer
 */
void gateway_ringbuf_reset(void* ringbuf);

/**
 * @brief Get available space for writing
 *
 * @param ringbuf Pointer to ring buffer
 * @return Number of bytes available
 */
uint32_t gateway_ringbuf_get_free(void* ringbuf);

/**
 * @brief Get available data for reading
 *
 * @param ringbuf Pointer to ring buffer
 * @return Number of bytes available
 */
uint32_t gateway_ringbuf_get_available(void* ringbuf);

/**
 * @brief Non-blocking write to ring buffer (Producer)
 *
 * @param ringbuf Pointer to ring buffer
 * @param data Pointer to data to write
 * @param size Size of data
 * @param bytes_written (optional) Pointer to store actual bytes written
 * @return GATEWAY_RINGBUF_OK on success, error code otherwise
 */
int gateway_ringbuf_write(void* ringbuf, const void* data, uint32_t size, uint32_t* bytes_written);

/**
 * @brief Non-blocking read from ring buffer (Consumer)
 *
 * @param ringbuf Pointer to ring buffer
 * @param buffer Buffer to store read data
 * @param size Size of buffer
 * @param bytes_read (optional) Pointer to store actual bytes read
 * @return GATEWAY_RINGBUF_OK on success, error code otherwise
 */
int gateway_ringbuf_read(void* ringbuf, void* buffer, uint32_t size, uint32_t* bytes_read);

/**
 * @brief Peek data without consuming (Consumer)
 *
 * @param ringbuf Pointer to ring buffer
 * @param buffer Buffer to store peeked data
 * @param size Size of buffer
 * @param bytes_read (optional) Pointer to store actual bytes peeked
 * @return GATEWAY_RINGBUF_OK on success, error code otherwise
 */
int gateway_ringbuf_peek(void* ringbuf, void* buffer, uint32_t size, uint32_t* bytes_read);

/**
 * @brief Skip bytes in buffer (Consumer)
 *
 * @param ringbuf Pointer to ring buffer
 * @param size Number of bytes to skip
 * @return GATEWAY_RINGBUF_OK on success, error code otherwise
 */
int gateway_ringbuf_skip(void* ringbuf, uint32_t size);

/*==============================================================================
 * LOCK-FREE RING BUFFER API - CORE 0 (Producer for buf_0_to_1, Consumer for buf_1_to_0)
 *============================================================================*/

/**
 * @brief Initialize Core 0 ring buffers
 *
 * @return 0 on success, -1 on error
 */
int gateway_ringbuf_core0_init(void);

/**
 * @brief Send data to Core 1 (Producer for buf_0_to_1)
 *
 * @param data Pointer to data
 * @param size Size of data
 * @param bytes_written (optional) Pointer to store actual bytes written
 * @return GATEWAY_RINGBUF_OK on success, error code otherwise
 */
int gateway_ringbuf_core0_send(const void* data, uint32_t size, uint32_t* bytes_written);

/**
 * @brief Receive data from Core 1 (Consumer for buf_1_to_0)
 *
 * @param buffer Buffer to store received data
 * @param size Size of buffer
 * @param bytes_read (optional) Pointer to store actual bytes read
 * @return GATEWAY_RINGBUF_OK on success, error code otherwise
 */
int gateway_ringbuf_core0_receive(void* buffer, uint32_t size, uint32_t* bytes_read);

/**
 * @brief Check available space in TX buffer (buf_0_to_1)
 *
 * @return Number of bytes available for writing
 */
uint32_t gateway_ringbuf_core0_get_free(void);

/**
 * @brief Check available data in RX buffer (buf_1_to_0)
 *
 * @return Number of bytes available for reading
 */
uint32_t gateway_ringbuf_core0_get_available(void);

/**
 * @brief Trigger IPC notification to Core 1
 *
 * @return 0 on success, -1 on error
 */
int gateway_ringbuf_core0_notify(void);

/*==============================================================================
 * LOCK-FREE RING BUFFER API - CORE 1 (Consumer for buf_0_to_1, Producer for buf_1_to_0)
 *============================================================================*/

/**
 * @brief Initialize Core 1 ring buffers
 *
 * @return 0 on success, -1 on error
 */
int gateway_ringbuf_core1_init(void);

/**
 * @brief Receive data from Core 0 (Consumer for buf_0_to_1)
 *
 * @param buffer Buffer to store received data
 * @param size Size of buffer
 * @param bytes_read (optional) Pointer to store actual bytes read
 * @return GATEWAY_RINGBUF_OK on success, error code otherwise
 */
int gateway_ringbuf_core1_receive(void* buffer, uint32_t size, uint32_t* bytes_read);

/**
 * @brief Send data to Core 0 (Producer for buf_1_to_0)
 *
 * @param data Pointer to data
 * @param size Size of data
 * @param bytes_written (optional) Pointer to store actual bytes written
 * @return GATEWAY_RINGBUF_OK on success, error code otherwise
 */
int gateway_ringbuf_core1_send(const void* data, uint32_t size, uint32_t* bytes_written);

/**
 * @brief Check available data in RX buffer (buf_0_to_1)
 *
 * @return Number of bytes available for reading
 */
uint32_t gateway_ringbuf_core1_get_available(void);

/**
 * @brief Check available space in TX buffer (buf_1_to_0)
 *
 * @return Number of bytes available for writing
 */
uint32_t gateway_ringbuf_core1_get_free(void);

/**
 * @brief Trigger IPC notification to Core 0
 *
 * @return 0 on success, -1 on error
 */
int gateway_ringbuf_core1_notify(void);

/*==============================================================================
 * LOCK-FREE RING BUFFER TEST DATA API
 *============================================================================*/

#endif /* GATEWAY_USE_LOCKFREE_RINGBUF */

#if GATEWAY_USE_PINGPONG_BUFFER
/*==============================================================================
 * PING-PONG BUFFER API (Experimental)
 *============================================================================*/

/* Core 0 Ping-Pong API */
int gateway_write_motor_commands_pp(const motor_cmd_ipc_t* cmds, uint16_t count);
int gateway_read_motor_states_pp(motor_state_ipc_t* states);

/* Core 1 Ping-Pong API */
int gateway_read_motor_commands_pp(motor_cmd_ipc_t* cmds);
int gateway_write_motor_states_pp(const motor_state_ipc_t* states, uint16_t count);

/* Debug API */
void gateway_pp_print_status(uint8_t core_id);
void gateway_pp_reset_stats(void);

#endif /* GATEWAY_USE_PINGPONG_BUFFER */

/*==============================================================================
 * MOTOR CONFIGURATION SYNCHRONIZATION API
 *============================================================================*/

/**
 * @brief Wait for Core 1 to populate motor configuration in shared memory
 *
 * Core 0 calls this to wait for Core 1 to initialize motor configuration.
 * Core 1 builds the motor lookup table and configuration after startup.
 *
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on timeout
 */
int gateway_wait_motor_config_ready(uint32_t timeout_ms);

/**
 * @brief Signal that motor configuration is ready (called by Core 1)
 *
 * Core 1 calls this after populating motor_config[] and g_motor_lookup[]
 */
void gateway_signal_motor_config_ready(void);

/*==============================================================================
 * QA DEBUG COUNTERS (for AGENT_QA instrumentation)
 *============================================================================*/

/**
 * @brief Debug counters for runtime instrumentation
 *
 * [QA TRACE T009] - Task T009: Instrument shared memory counters
 * These counters provide visibility into IPC, CAN, UDP, IMU, and error stats.
 * Placed at end of shared memory (offset 0x8000 from base 0x701D0000).
 * All counters are accessible via JTAG for real-time debugging.
 *
 * Memory Layout (offset 0x8000):
 *   0x00: dbg_ipc_send_count    (T023) - IPC sends Core0→Core1
 *   0x04: dbg_ipc_recv_count    (T024) - IPC recvs Core1→Core0
 *   0x08: dbg_can_rx_count      (T025) - CAN RX frames
 *   0x0C: dbg_can_tx_count      (T026) - CAN TX frames
 *   0x10: dbg_udp_rx_count      (T027) - UDP RX packets
 *   0x14: dbg_udp_tx_count      (T028) - UDP TX packets
 *   0x18: dbg_imu_frame_count   (T029) - IMU frames
 *   0x1C: dbg_error_count       (T030) - Total errors
 *   0x20: dbg_last_error_code   (T031) - Last error
 *   0x24: dbg_ipc_register_count        - IPC registration count [T015]
 *
 * All counters are uint32_t, volatile for multi-core access
 * Must use atomic operations for safety
 */

/* Debug counter definitions */
extern volatile uint32_t dbg_ipc_send_count;      /* [QA TRACE T023] */
extern volatile uint32_t dbg_ipc_recv_count;      /* [QA TRACE T024] */
extern volatile uint32_t dbg_can_rx_count;        /* [QA TRACE T025] */
extern volatile uint32_t dbg_can_tx_count;        /* [QA TRACE T026] */
extern volatile uint32_t dbg_udp_rx_count;        /* [QA TRACE T027] */
extern volatile uint32_t dbg_udp_tx_count;        /* [QA TRACE T028] */
extern volatile uint32_t dbg_imu_frame_count;     /* [QA TRACE T029] */
extern volatile uint32_t dbg_error_count;         /* [QA TRACE T030] */
extern volatile uint32_t dbg_last_error_code;     /* [QA TRACE T031] */
extern volatile uint32_t dbg_ipc_register_count;  /* [QA TRACE T015] */
extern volatile uint32_t dbg_imu_uart_isr_count;  /* [DEBUG] IMU UART ISR call count */
extern volatile uint32_t dbg_imu_rx_byte_count;   /* [DEBUG] IMU RX byte count */

/**
 * @brief Atomic counter increment macro (ARM Cortex-R5F)
 *
 * Uses inline assembly for atomic increment with memory barriers.
 * Safe for both task and ISR context on both cores.
 *
 * Usage: DEBUG_COUNTER_INC(dbg_can_rx_count);
 */
#define DEBUG_COUNTER_INC(counter) do { \
    __asm__ volatile ( \
        "1:  ldr  r1, [%0]\n\t" \
        "    add  r1, r1, #1\n\t" \
        "    dmb\n\t" \
        "    str  r1, [%0]\n\t" \
        "    dmb" \
        : "+r"(counter) \
        : \
        : "r1", "memory" \
    ); \
} while(0)

/**
 * @brief Set error code (atomic)
 *
 * Usage: DEBUG_SET_ERROR(0x1234);
 */
#define DEBUG_SET_ERROR(code) do { \
    __asm__ volatile ( \
        "ldr r1, =%0\n\t" \
        "ldr r2, =%1\n\t" \
        "dmb\n\t" \
        "str r2, [r1]\n\t" \
        "dmb" \
        : \
        : "i"(dbg_last_error_code), "i"(code) \
        : "r1", "r2", "memory" \
    ); \
} while(0)

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_SHARED_H_ */
