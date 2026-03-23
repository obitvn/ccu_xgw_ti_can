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
 * CONSTANTS
 *============================================================================*/

#define GATEWAY_SHARED_MAGIC        0x47444154  /* "GTAD" */
#define GATEWAY_SHARED_VERSION      0x00030003  /* Version 3.0.0 */

/* Shared Memory Configuration */
#define GATEWAY_NUM_MOTORS          4       /* TEMP: Reduce from 23 to 4 for testing */
#define GATEWAY_NUM_CAN_BUSES       8
#define GATEWAY_SHARED_MEM_ADDR     0x701D0000
#define GATEWAY_SHARED_MEM_SIZE     0x4000      /* 16KB */

/* IPC Message IDs (IpcNotify) */
#define MSG_CAN_DATA_READY          0x01    /* Core 1 -> Core 0: Motor states available */
#define MSG_ETH_DATA_READY          0x02    /* Core 0 -> Core 1: Motor commands available */
#define MSG_CAN_DATA_ACK            0x03    /* Core 0 -> Core 1: Ack states received */
#define MSG_ETH_DATA_ACK            0x04    /* Core 1 -> Core 0: Ack commands received */
#define MSG_RS485_DATA_READY        0x05    /* Future: RS485 data */
#define MSG_IMU_DATA_READY          0x06    /* Future: IMU data */
#define MSG_HEARTBEAT               0x07    /* Heartbeat sync */
#define MSG_EMERGENCY_STOP          0x08    /* Emergency stop signal */
#define MSG_TEST_DATA_READY         0x09    /* Core 0 -> Core 1: Test data ready */
#define MSG_TEST_DATA_FROM_CORE1    0x0A    /* Core 1 -> Core 0: Test data from Core 1 */

/* Feature flag: Enable Ping-Pong Buffer (set to 1 to enable) */
#define GATEWAY_USE_PINGPONG_BUFFER  0

/* IPC Client ID - BOTH cores must register the SAME client ID for IPC communication */
#define GATEWAY_IPC_CLIENT_ID        4U     /* Common client ID for both cores */

/* Legacy definitions (kept for compatibility but not used for IPC Notify) */
#define CLIENT_ID_CAN_RX            0       /* Core 1: CAN RX - DEPRECATED */
#define CLIENT_ID_ETH_TX            1       /* Core 0: Ethernet TX - DEPRECATED */
#define CLIENT_ID_CAN_TX            2       /* Core 1: CAN TX - DEPRECATED */
#define CLIENT_ID_ETH_RX            3       /* Core 0: Ethernet RX - DEPRECATED */

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

/**
 * @brief Test data buffer for shared memory testing
 */
typedef struct {
    uint32_t data[16];          /* Test data array */
    volatile uint32_t ready;    /* Data ready flag */
    volatile uint32_t sequence; /* Sequence number */
    uint32_t reserved[3];
} test_data_buf_t;

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
 * Total size: ~2KB (single buffer) or ~4KB (ping-pong buffer)
 * Placed at 0x701D0000 with non-cacheable MPU settings
 */
typedef struct __attribute__((packed)) {
    /* === Header Section (128 bytes) === */
    uint32_t magic;                                /* 0x47444154 ("GTAD") */
    uint32_t version;                              /* Structure version */
    volatile uint32_t heartbeat_r5f0_0;            /* Core 0 heartbeat counter */
    volatile uint32_t heartbeat_r5f0_1;            /* Core 1 heartbeat counter */
    test_data_buf_t test_data;                     /* NEW: Test data buffer (shared) */
    uint32_t reserved[20];                         /* Reduced reserved for test_data */

#if GATEWAY_USE_PINGPONG_BUFFER
    /* === Ping-Pong Buffers === */
    can_to_eth_ppbuf_t can_to_eth_ppbuf;
    eth_to_can_ppbuf_t eth_to_can_ppbuf;
#else
    /* === Single Buffers === */
    can_to_eth_buf_t can_to_eth_buf;
    eth_to_can_buf_t eth_to_can_buf;
#endif

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

/*==============================================================================
 * TEST DATA API (Shared Memory Testing)
 *============================================================================*/

/**
 * @brief Write test data to shared memory (Core 0 -> Core 1 or Core 1 -> Core 0)
 *
 * @param data Input array (up to 16 elements)
 * @param count Number of elements to write
 * @return 0 on success, -1 on error
 */
int gateway_write_test_data(const uint32_t* data, uint16_t count);

/**
 * @brief Read test data from shared memory
 *
 * @param data Output array (up to 16 elements)
 * @return Number of elements read, 0 if no data ready
 */
int gateway_read_test_data(uint32_t* data);

/**
 * @brief Notify other core that test data is ready
 */
int gateway_notify_test_data_ready(void);

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
