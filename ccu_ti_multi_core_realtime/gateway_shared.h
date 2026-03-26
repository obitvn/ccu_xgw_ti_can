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
#define GATEWAY_NUM_MOTORS          4       /* TEMP: Reduce from 23 to 4 for testing */
#define GATEWAY_NUM_CAN_BUSES       8
#define GATEWAY_SHARED_MEM_ADDR     0x701D0000
#define GATEWAY_SHARED_MEM_SIZE     0x8000      /* 32KB */

/* Lock-free Ring Buffer Configuration */
#define GATEWAY_USE_LOCKFREE_RINGBUF    1   /* Enable lock-free ring buffer */

/* Ring Buffer Sizes (power of 2 for efficient modulo) */
#define GATEWAY_RINGBUF_0_TO_1_SIZE     (8 * 1024)   /* 8KB: Core0 -> Core1 (power of 2) */
#define GATEWAY_RINGBUF_1_TO_0_SIZE     (8 * 1024)   /* 8KB: Core1 -> Core0 (power of 2) */
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
 * @brief Motor command for IPC (Ethernet -> CAN)
 *
 * Optimized packed structure for shared memory transfer
 */
typedef struct __attribute__((packed)) {
    uint8_t  motor_id;      /* Motor ID (1-127) */
    uint8_t  can_bus;       /* CAN bus (0-7) */
    uint8_t  mode;          /* Mode (0=disable, 1=enable) */
    uint8_t  reserved;
    uint16_t position;      /* Position (0.01 rad) */
    int16_t  velocity;      /* Velocity (0.01 rad/s) */
    int16_t  torque;        /* Torque (0.01 Nm) */
    uint16_t kp;            /* Kp gain (0.01) */
    uint16_t kd;            /* Kd gain (0.01) */
} motor_cmd_ipc_t;

/**
 * @brief Motor state for IPC (CAN -> Ethernet)
 *
 * Optimized packed structure for shared memory transfer
 */
typedef struct __attribute__((packed)) {
    uint8_t  motor_id;      /* Motor ID (1-127) */
    uint8_t  can_bus;       /* CAN bus (0-7) */
    int8_t   pattern;       /* Control pattern */
    uint8_t  error_code;    /* Error code */
    uint16_t position;      /* Position (0.01 rad) */
    int16_t  velocity;      /* Velocity (0.01 rad/s) */
    int16_t  torque;        /* Torque (0.01 Nm) */
    int16_t  temperature;   /* Temperature (0.1 °C) */
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
    uint32_t reserved[28];                         /* Alignment padding */

#if GATEWAY_USE_LOCKFREE_RINGBUF
    /* === Lock-free Ring Buffers === */
    /* Buffer 0: Core0 (Producer) -> Core1 (Consumer) */
    uint8_t ringbuf_0_to_1[sizeof(Gateway_RingBuf_Control_t) + GATEWAY_RINGBUF_0_TO_1_SIZE];

    /* Buffer 1: Core1 (Producer) -> Core0 (Consumer) */
    uint8_t ringbuf_1_to_0[sizeof(Gateway_RingBuf_Control_t) + GATEWAY_RINGBUF_1_TO_0_SIZE];
#endif

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

/**
 * @brief Test data packet for ring buffer testing
 */
typedef struct {
    uint32_t sequence;       /* Sequence number */
    uint32_t timestamp;      /* Timestamp (cycle count) */
    uint32_t data[8];        /* Test data */
    uint32_t checksum;       /* Simple checksum */
} ringbuf_test_data_t;

_Static_assert(sizeof(ringbuf_test_data_t) < 256, "Test data too large");

/**
 * @brief Write test data to ring buffer (Core 0 -> Core 1)
 *
 * @param test_data Pointer to test data
 * @return GATEWAY_RINGBUF_OK on success
 */
int gateway_ringbuf_write_test_core0(const ringbuf_test_data_t* test_data);

/**
 * @brief Read test data from ring buffer (Core 1 receives from Core 0)
 *
 * @param test_data Pointer to store test data
 * @return GATEWAY_RINGBUF_OK on success
 */
int gateway_ringbuf_read_test_core1(ringbuf_test_data_t* test_data);

/**
 * @brief Write test data to ring buffer (Core 1 -> Core 0)
 *
 * @param test_data Pointer to test data
 * @return GATEWAY_RINGBUF_OK on success
 */
int gateway_ringbuf_write_test_core1(const ringbuf_test_data_t* test_data);

/**
 * @brief Read test data from ring buffer (Core 0 receives from Core 1)
 *
 * @param test_data Pointer to store test data
 * @return GATEWAY_RINGBUF_OK on success
 */
int gateway_ringbuf_read_test_core0(ringbuf_test_data_t* test_data);

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

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_SHARED_H_ */
