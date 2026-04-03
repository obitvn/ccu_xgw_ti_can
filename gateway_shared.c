/**
 * @file gateway_shared.c
 * @brief Shared Memory IPC Implementation for CCU Multicore Gateway
 *
 * Lock-free shared memory communication between R5F0-0 and R5F0-1
 * - Lock-free ring buffer with cache line alignment
 * - Memory barriers for ARM Cortex-R5F
 *
 * @author CCU Multicore Project
 * @date 2026-03-18
 * @date 2026-03-26 - Added lock-free ring buffer support
 * @date 2026-03-29 - Added emergency stop handler (S006)
 */

#include "gateway_shared.h"
#include <kernel/dpl/DebugP.h>
#include <string.h>
#include <drivers/gpio.h>  /* For emergency GPIO output (optional) */

/*==============================================================================
 * SHARED MEMORY INSTANCE
 *============================================================================*/

/**
 * @brief Shared memory instance
 *
 * Placed in .bss.user_shared_mem section (0x701D0000)
 * MPU must be configured as Non-cacheable for this region
 */
volatile GatewaySharedData_t gGatewaySharedMem
    __attribute__((section(".bss.user_shared_mem"), aligned(128)));

/* Memory barrier helper */
static inline void gateway_memory_barrier(void)
{
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("dmb" ::: "memory");
}

/*==============================================================================
 * CORE 0 (FreeRTOS, Ethernet) IMPLEMENTATION
 *============================================================================*/

/**
 * @brief Initialize Core 0 shared memory access
 */
int gateway_core0_init(void)
{
    /* Core 0 is the master: initialize the shared memory structure */
    DebugP_log("[Core0] Initializing shared memory at 0x%08X...\r\n",
               (uint32_t)&gGatewaySharedMem);

    memset((void*)&gGatewaySharedMem, 0, sizeof(GatewaySharedData_t));

#if GATEWAY_USE_PINGPONG_BUFFER
    /* Initialize ping-pong buffer indices */
    gGatewaySharedMem.can_to_eth_ppbuf.write_idx = 0;
    gGatewaySharedMem.can_to_eth_ppbuf.read_idx = 0;
    gGatewaySharedMem.can_to_eth_ppbuf.write_done[0] = 0;
    gGatewaySharedMem.can_to_eth_ppbuf.write_done[1] = 0;
    gGatewaySharedMem.can_to_eth_ppbuf.write_seq[0] = 0;
    gGatewaySharedMem.can_to_eth_ppbuf.write_seq[1] = 0;

    gGatewaySharedMem.eth_to_can_ppbuf.write_idx = 0;
    gGatewaySharedMem.eth_to_can_ppbuf.read_idx = 0;
    gGatewaySharedMem.eth_to_can_ppbuf.write_done[0] = 0;
    gGatewaySharedMem.eth_to_can_ppbuf.write_done[1] = 0;
    gGatewaySharedMem.eth_to_can_ppbuf.write_seq[0] = 0;
    gGatewaySharedMem.eth_to_can_ppbuf.write_seq[1] = 0;
#endif

    /* 
     * We don't set magic yet. 
     * We will set it in a separate function AFTER IPC sync.
     */
    return 0;
}

/**
 * @brief Finalize Core 0 initialization (call AFTER IPC sync)
 */
int gateway_core0_finalize(void)
{
    /* Set magic and version LAST to signal Core 1 that we are ready */
    gateway_memory_barrier();
    gGatewaySharedMem.version = GATEWAY_SHARED_VERSION;
    gGatewaySharedMem.magic = GATEWAY_SHARED_MAGIC;
    gateway_memory_barrier();

    DebugP_log("[Core0] Shared memory Magic set, Core 1 can proceed\r\n");
    return 0;
}

/**
 * @brief Write motor commands to shared memory (Core 0 -> Core 1)
 */
int gateway_write_motor_commands(const motor_cmd_ipc_t* cmds, uint16_t count)
{
#if GATEWAY_USE_PINGPONG_BUFFER
    /* Use ping-pong buffer implementation */
    return gateway_write_motor_commands_pp(cmds, count);
#else
    if (cmds == NULL || count > GATEWAY_NUM_MOTORS) {
        return -1;
    }

    /* Write commands to shared memory */
    for (uint16_t i = 0; i < count; i++) {
        gGatewaySharedMem.eth_to_can_buf.motors[i] = cmds[i];
    }

    /* Set ready flag and increment sequence */
    gateway_memory_barrier();
    gGatewaySharedMem.eth_to_can_buf.ready_flag = 1;
    gGatewaySharedMem.eth_to_can_buf.sequence++;
    gateway_memory_barrier();

    /* Update statistics */
    gGatewaySharedMem.stats.udp_rx_count++;

    return count;
#endif
}

/**
 * @brief Read motor states from shared memory (Core 0 reads from Core 1)
 */
int gateway_read_motor_states(motor_state_ipc_t* states)
{
#if GATEWAY_USE_PINGPONG_BUFFER
    /* Use ping-pong buffer implementation */
    return gateway_read_motor_states_pp(states);
#else
    if (states == NULL) {
        return -1;
    }

    /* Check if data is ready */
    if (gGatewaySharedMem.can_to_eth_buf.ready_flag == 0) {
        return 0;  /* No new data */
    }

    gateway_memory_barrier();

    /* Copy motor states */
    for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
        states[i] = gGatewaySharedMem.can_to_eth_buf.motors[i];
    }

    /* Clear ready flag */
    gGatewaySharedMem.can_to_eth_buf.ready_flag = 0;
    gateway_memory_barrier();

    return GATEWAY_NUM_MOTORS;
#endif
}

/**
 * @brief IPC notification callback for Core 0
 */
void gateway_core0_ipc_callback(uint16_t clientId, uint16_t msg)
{
    switch (msg) {
        case MSG_CAN_DATA_READY:
            /* Core 1 has written new motor states */
            gGatewaySharedMem.stats.ipc_notify_count[clientId & 0x3]++;
            break;

#if GATEWAY_USE_PINGPONG_BUFFER
        case MSG_ETH_DATA_ACK:
            /* Core 1 has received our motor commands */
            {
                volatile eth_to_can_ppbuf_t* pp = &gGatewaySharedMem.eth_to_can_ppbuf;
                uint32_t write_idx = pp->write_idx;
                if (pp->write_done[write_idx] != 0) {
                    pp->write_done[write_idx] = 0;
                    DebugP_log("[Core0] PP: buf[%u] ACKed\r\n", write_idx);
                }
            }
            gGatewaySharedMem.stats.ipc_notify_count[clientId & 0x3]++;
            break;
#endif

        case MSG_HEARTBEAT:
            /* Heartbeat from Core 1 */
            gGatewaySharedMem.stats.ipc_notify_count[clientId & 0x3]++;
            break;

        default:
            DebugP_log("[Core0] Unknown IPC message: 0x%02X\r\n", msg);
            break;
    }
}

/*==============================================================================
 * CORE 1 (NoRTOS, CAN) IMPLEMENTATION
 *============================================================================*/

/**
 * @brief Initialize Core 1 shared memory access
 */
int gateway_core1_init(void)
{
    /* 
     * IMPORTANT: Do NOT busy-wait here if this is called before IpcNotify_syncAll.
     * Core 1 should just return and wait for the magic AFTER IPC sync is complete.
     */
    if (gGatewaySharedMem.magic == GATEWAY_SHARED_MAGIC) {
        DebugP_log("[Core1] Shared memory already initialized\r\n");
    } else {
        DebugP_log("[Core1] Shared memory not yet ready, will wait after IPC sync\r\n");
    }

    return 0;
}

/**
 * @brief Wait for Core 0 to initialize shared memory
 * Call this AFTER IpcNotify_syncAll
 */
int gateway_core1_wait_for_ready(void)
{
    uint32_t timeout = 0xFFFFFF;
    
    DebugP_log("[Core1] Waiting for Core 0 Magic...\r\n");
    
    while (gGatewaySharedMem.magic != GATEWAY_SHARED_MAGIC && timeout > 0) {
        timeout--;
        /* Optional: Small delay to reduce bus contention */
        for(volatile int i=0; i<100; i++);
    }

    if (timeout == 0) {
        DebugP_log("[Core1] ERROR: Shared memory initialization timeout!\r\n");
        return -1;
    }

    gateway_memory_barrier();
    DebugP_log("[Core1] Shared memory access ready at 0x%08X\r\n",
               (uint32_t)&gGatewaySharedMem);
               
    return 0;
}

/**
 * @brief Read motor commands from shared memory (Core 1 reads from Core 0)
 */
int gateway_read_motor_commands_core1(motor_cmd_ipc_t* cmds)
{
#if GATEWAY_USE_PINGPONG_BUFFER
    /* Use ping-pong buffer implementation */
    return gateway_read_motor_commands_pp(cmds);
#else
    if (cmds == NULL) {
        return -1;
    }

    /* Check if new commands are available */
    if (gGatewaySharedMem.eth_to_can_buf.ready_flag == 0) {
        return 0;  /* No new commands */
    }

    gateway_memory_barrier();

    /* Copy motor commands */
    for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
        cmds[i] = gGatewaySharedMem.eth_to_can_buf.motors[i];
    }

    /* Clear ready flag */
    gGatewaySharedMem.eth_to_can_buf.ready_flag = 0;
    gateway_memory_barrier();

    return GATEWAY_NUM_MOTORS;
#endif
}

/**
 * @brief Write motor states to shared memory (Core 1 -> Core 0)
 */
int gateway_write_motor_states(const motor_state_ipc_t* states, uint16_t count)
{
#if GATEWAY_USE_PINGPONG_BUFFER
    /* Use ping-pong buffer implementation */
    return gateway_write_motor_states_pp(states, count);
#else
    if (states == NULL || count > GATEWAY_NUM_MOTORS) {
        return -1;
    }

    /* Write states to shared memory */
    for (uint16_t i = 0; i < count; i++) {
        gGatewaySharedMem.can_to_eth_buf.motors[i] = states[i];
    }

    /* Set ready flag and increment sequence */
    gateway_memory_barrier();
    gGatewaySharedMem.can_to_eth_buf.ready_flag = 1;
    gGatewaySharedMem.can_to_eth_buf.sequence++;
    gateway_memory_barrier();

    /* Update statistics */
    gGatewaySharedMem.stats.can_rx_count++;

    return count;
#endif
}

/**
 * @brief IPC notification callback for Core 1
 *
 * Called from IPC interrupt when Core 0 sends notification
 */
void gateway_core1_ipc_callback(uint16_t clientId, uint16_t msg)
{
    switch (msg) {
        case MSG_ETH_DATA_READY:
            /* Core 0 has written new motor commands */
            gGatewaySharedMem.stats.ipc_notify_count[clientId]++;
            break;

#if GATEWAY_USE_PINGPONG_BUFFER
        case MSG_CAN_DATA_ACK:
            /* Core 0 has received our motor states */
            {
                volatile can_to_eth_ppbuf_t* pp = &gGatewaySharedMem.can_to_eth_ppbuf;
                uint32_t write_idx = pp->write_idx;
                if (pp->write_done[write_idx] != 0) {
                    pp->write_done[write_idx] = 0;
                    DebugP_log("[Core1] PP: buf[%u] ACKed\r\n", write_idx);
                }
            }
            gGatewaySharedMem.stats.ipc_notify_count[clientId]++;
            break;
#endif

        case MSG_HEARTBEAT:
            /* Heartbeat from Core 0 */
            gGatewaySharedMem.stats.ipc_notify_count[clientId]++;
            break;

        case MSG_EMERGENCY_STOP:
            /* Emergency stop - disable all CAN buses
             * [STUB S002] - Implemented emergency stop handler */
            DebugP_log("[Core1] *** EMERGENCY STOP received from Core0 ***\r\n");
            gateway_set_emergency_stop();
            break;

        default:
            DebugP_log("[Core1] Unknown IPC message: 0x%02X\r\n", msg);
            break;
    }
}

/*==============================================================================
 * SHARED UTILITIES
 *============================================================================*/

/**
 * @brief Update heartbeat counter
 */
void gateway_update_heartbeat(uint8_t core_id)
{
    if (core_id == 0) {
        gGatewaySharedMem.heartbeat_r5f0_0++;
    } else if (core_id == 1) {
        gGatewaySharedMem.heartbeat_r5f0_1++;
    }
}

/**
 * @brief Get heartbeat counter
 */
uint32_t gateway_get_heartbeat(uint8_t core_id)
{
    if (core_id == 0) {
        return gGatewaySharedMem.heartbeat_r5f0_0;
    } else if (core_id == 1) {
        return gGatewaySharedMem.heartbeat_r5f0_1;
    }
    return 0;
}

/**
 * @brief Check if both cores are alive
 */
bool gateway_check_heartbeat(void)
{
    uint32_t hb0 = gGatewaySharedMem.heartbeat_r5f0_0;
    uint32_t hb1 = gGatewaySharedMem.heartbeat_r5f0_1;

    /* Both heartbeats should be incrementing */
    return (hb0 > 0) && (hb1 > 0);
}

/**
 * @brief Update statistics
 */
void gateway_update_stat(uint8_t core_id, uint8_t stat_id)
{
    (void)core_id;

    switch (stat_id) {
        case 0: /* CAN TX */
            gGatewaySharedMem.stats.can_tx_count++;
            break;
        case 1: /* UDP TX */
            gGatewaySharedMem.stats.udp_tx_count++;
            break;
        default:
            break;
    }
}

#include <drivers/ipc_notify.h>

/* Core ID definitions */
#ifndef CSL_CORE_ID_R5FSS0_0
#define CSL_CORE_ID_R5FSS0_0  (0U)
#endif
#ifndef CSL_CORE_ID_R5FSS0_1
#define CSL_CORE_ID_R5FSS0_1  (1U)
#endif

int gateway_notify_commands_ready(void)
{
#if GATEWAY_USE_PINGPONG_BUFFER
    return IpcNotify_sendMsg(CSL_CORE_ID_R5FSS0_1, GATEWAY_IPC_CLIENT_ID, MSG_ETH_DATA_READY, 1);
#else
    return IpcNotify_sendMsg(CSL_CORE_ID_R5FSS0_1, CLIENT_ID_ETH_TX, MSG_ETH_DATA_READY, 1);
#endif
}

int gateway_notify_states_ready(void)
{
#if GATEWAY_USE_PINGPONG_BUFFER
    return IpcNotify_sendMsg(CSL_CORE_ID_R5FSS0_0, GATEWAY_IPC_CLIENT_ID, MSG_CAN_DATA_READY, 1);
#else
    return IpcNotify_sendMsg(CSL_CORE_ID_R5FSS0_0, CLIENT_ID_CAN_RX, MSG_CAN_DATA_READY, 1);
#endif
}

/*==============================================================================
 * IMU SHARED MEMORY FUNCTIONS
 *============================================================================*/

int gateway_write_imu_state(const imu_state_ipc_t* imu_state)
{
    if (imu_state == NULL) {
        return -1;
    }

    /* Copy IMU state to shared memory */
    gGatewaySharedMem.imu_state = *imu_state;

    /* Update sequence and set ready flag */
    gGatewaySharedMem.imu_sequence++;
    gateway_memory_barrier();
    gGatewaySharedMem.imu_ready_flag = 1;
    gateway_memory_barrier();

    /* [QA TRACE T029] Increment IMU frame counter */
    DEBUG_COUNTER_INC(dbg_imu_frame_count);

    return 0;
}

int gateway_read_imu_state(imu_state_ipc_t* imu_state)
{
    if (imu_state == NULL) {
        return -1;
    }

    /* Check if IMU data is ready */
    if (gGatewaySharedMem.imu_ready_flag == 0) {
        return -1;  /* No new data */
    }

    gateway_memory_barrier();

    /* Copy IMU state from shared memory */
    *imu_state = gGatewaySharedMem.imu_state;

    /* Clear ready flag */
    gGatewaySharedMem.imu_ready_flag = 0;

    return 0;
}

int gateway_notify_imu_ready(void)
{
    return IpcNotify_sendMsg(CSL_CORE_ID_R5FSS0_0, GATEWAY_IPC_CLIENT_ID, MSG_IMU_DATA_READY, 1);
}

/*==============================================================================
 * EMERGENCY STOP HANDLERS
 *============================================================================*/

/**
 * @brief Core 0: Emergency stop handler
 *
 * Called when emergency stop is triggered (from Core 1 via IPC or locally).
 * Implements basic emergency stop actions:
 * 1. Log event to syslog/UART
 * 2. Set shared memory emergency flag
 * 3. Notify application via IPC to Core 1
 * 4. Optional: Set emergency GPIO output (if configured)
 *
 * @note This is a CORE 0 ONLY function
 * @note Emergency GPIO is optional - requires CONFIG_GPIO_EMERGENCY_PIN to be defined
 * @note Motor shutdown is handled by Core 1 via IPC notification
 *
 * Implementation Details:
 * - Uses DebugP_log for immediate UART output
 * - Uses LOG_ALERT for syslog integration (if enabled)
 * - Sets emergency_stop_flag in shared memory for both cores
 * - Sends MSG_EMERGENCY_STOP to Core 1 to trigger motor shutdown
 * - Optional: Sets GPIO pin high if emergency GPIO is configured
 *
 * [STUB S006] - Implemented basic emergency stop handler
 */
void gateway_core0_emergency_stop_handler(void)
{
    /* 1. Log event immediately to UART (always available) */
    DebugP_log("[Core0] *** EMERGENCY STOP TRIGGERED ***\r\n");

    /* 2. Log to syslog if enabled (LOG_ALERT = highest priority) */
    #ifdef SYSLOG_REDIRECT_DEBUGP
    LOG_ALERT("Emergency stop triggered on Core0");
    #endif

    /* 3. Set shared memory emergency flag (visible to both cores) */
    gateway_memory_barrier();
    gGatewaySharedMem.emergency_stop_flag = 1;
    gateway_memory_barrier();

    /* 4. Update error statistics */
    gGatewaySharedMem.stats.error_count++;

    /* 5. Notify Core 1 to stop all motors via IPC */
    /* Core 1 will handle CAN bus shutdown and motor stop commands */
    IpcNotify_sendMsg(CSL_CORE_ID_R5FSS0_1, GATEWAY_IPC_CLIENT_ID, MSG_EMERGENCY_STOP, 1);

    /* 6. Optional: Set emergency GPIO output (if configured) */
    /* This requires a GPIO pin to be configured in SysConfig with CONFIG_GPIO_EMERGENCY_PIN */
    #ifdef CONFIG_GPIO_EMERGENCY_PIN
    #ifdef CONFIG_GPIO_EMERGENCY_BASE_ADDR
    GPIO_pinWriteHigh(CONFIG_GPIO_EMERGENCY_BASE_ADDR, CONFIG_GPIO_EMERGENCY_PIN);
    DebugP_log("[Core0] Emergency GPIO set HIGH\r\n");
    #endif
    #endif

    /* 7. Notify application callback (if registered) */
    /* Application can implement custom emergency stop logic */
    /* TODO: Add callback registration mechanism if needed */

    DebugP_log("[Core0] Emergency stop handling complete\r\n");
}

/**
 * @brief Set emergency stop flag (shared function for both cores)
 *
 * Sets the shared memory emergency_stop_flag to notify both cores.
 * Can be called from either core when emergency stop condition is detected.
 *
 * [STUB S002] - Implemented emergency stop handler
 */
void gateway_set_emergency_stop(void)
{
    /* Set shared memory emergency flag (visible to both cores) */
    gateway_memory_barrier();
    gGatewaySharedMem.emergency_stop_flag = 1;
    gateway_memory_barrier();

    /* Update error statistics */
    gGatewaySharedMem.stats.error_count++;

    DebugP_log("[Gateway] Emergency stop flag set\r\n");
}

/**
 * @brief Check emergency stop flag
 *
 * @return 1 if emergency stop is active, 0 otherwise
 */
int gateway_check_emergency_stop(void)
{
    gateway_memory_barrier();
    return (gGatewaySharedMem.emergency_stop_flag != 0) ? 1 : 0;
}

/**
 * @brief Clear emergency stop flag
 *
 * Should be called after handling the emergency stop condition
 * to resume normal operation.
 */
void gateway_clear_emergency_stop(void)
{
    gateway_memory_barrier();
    gGatewaySharedMem.emergency_stop_flag = 0;
    gateway_memory_barrier();

    DebugP_log("[Gateway] Emergency stop flag cleared\r\n");
}

#if GATEWAY_USE_PINGPONG_BUFFER
/*==============================================================================
 * PING-PONG BUFFER IMPLEMENTATION (Experimental)
 *============================================================================*/

/* Helper: Get next buffer index */
static inline uint32_t pp_next_idx(uint32_t idx)
{
    return 1U - idx;
}

/*==============================================================================
 * CORE 0 (FreeRTOS, Ethernet) - Ping-Pong Implementation
 *============================================================================*/

int gateway_write_motor_commands_pp(const motor_cmd_ipc_t* cmds, uint16_t count)
{
    if (cmds == NULL || count > GATEWAY_NUM_MOTORS) {
        return -1;
    }

    volatile eth_to_can_ppbuf_t* pp = &gGatewaySharedMem.eth_to_can_ppbuf;
    uint32_t write_idx = pp->write_idx;
    uint32_t next_idx = pp_next_idx(write_idx);

    /* Check if current write buffer is available */
    if (pp->write_done[write_idx] != 0) {
        /* Current buffer still being read by Core 1 */
        if (pp->write_done[next_idx] == 0) {
            /* Other buffer is available, switch */
            write_idx = next_idx;
            pp->write_idx = write_idx;
            pp->stats.buffer_switch_count++;
            DebugP_log("[Core0] PP: Switch to buf[%u]\r\n", write_idx);
        } else {
            /* Both buffers busy */
            pp->stats.timeout_count++;
            return 0;
        }
    }

    /* Write commands to buffer */
    for (uint16_t i = 0; i < count; i++) {
        pp->buffer[write_idx][i] = cmds[i];
    }

    /* Update sequence and mark as ready */
    pp->write_seq[write_idx]++;
    gateway_memory_barrier();
    pp->write_done[write_idx] = 1;
    gateway_memory_barrier();
    pp->stats.write_count++;

    return count;
}

int gateway_read_motor_states_pp(motor_state_ipc_t* states)
{
    if (states == NULL) {
        return -1;
    }

    volatile can_to_eth_ppbuf_t* pp = &gGatewaySharedMem.can_to_eth_ppbuf;
    uint32_t read_idx = pp->read_idx;

    /* Check if data is available */
    if (pp->write_done[read_idx] == 0) {
        return 0;  /* No new data */
    }

    gateway_memory_barrier();

    /* Check sequence */
    uint32_t seq = pp->write_seq[read_idx];
    if (pp->stats.last_seq != 0 && seq != pp->stats.last_seq + 1) {
        pp->stats.seq_error_count++;
    }
    pp->stats.last_seq = seq;

    /* Copy motor states */
    for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
        states[i] = pp->buffer[read_idx][i];
    }

    /* Mark buffer as consumed and send ACK */
    pp->write_done[read_idx] = 0;
    pp->stats.read_count++;

    IpcNotify_sendMsg(CSL_CORE_ID_R5FSS0_1, GATEWAY_IPC_CLIENT_ID, MSG_CAN_DATA_ACK, 1);

    /* Switch to next buffer */
    pp->read_idx = pp_next_idx(read_idx);

    return GATEWAY_NUM_MOTORS;
}

/*==============================================================================
 * CORE 1 (NoRTOS, CAN) - Ping-Pong Implementation
 *============================================================================*/

int gateway_read_motor_commands_pp(motor_cmd_ipc_t* cmds)
{
    if (cmds == NULL) {
        return -1;
    }

    volatile eth_to_can_ppbuf_t* pp = &gGatewaySharedMem.eth_to_can_ppbuf;
    uint32_t read_idx = pp->read_idx;

    /* Check if data is available */
    if (pp->write_done[read_idx] == 0) {
        return 0;
    }

    gateway_memory_barrier();

    /* Check sequence */
    uint32_t seq = pp->write_seq[read_idx];
    if (pp->stats.last_seq != 0 && seq != pp->stats.last_seq + 1) {
        pp->stats.seq_error_count++;
    }
    pp->stats.last_seq = seq;

    /* Copy motor commands */
    for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
        cmds[i] = pp->buffer[read_idx][i];
    }

    /* Mark buffer as consumed and send ACK */
    pp->write_done[read_idx] = 0;
    pp->stats.read_count++;

    IpcNotify_sendMsg(CSL_CORE_ID_R5FSS0_0, GATEWAY_IPC_CLIENT_ID, MSG_ETH_DATA_ACK, 1);

    /* Switch to next buffer */
    pp->read_idx = pp_next_idx(read_idx);

    return GATEWAY_NUM_MOTORS;
}

int gateway_write_motor_states_pp(const motor_state_ipc_t* states, uint16_t count)
{
    if (states == NULL || count > GATEWAY_NUM_MOTORS) {
        return -1;
    }

    volatile can_to_eth_ppbuf_t* pp = &gGatewaySharedMem.can_to_eth_ppbuf;
    uint32_t write_idx = pp->write_idx;
    uint32_t next_idx = pp_next_idx(write_idx);

    /* Check if current write buffer is available */
    if (pp->write_done[write_idx] != 0) {
        if (pp->write_done[next_idx] == 0) {
            write_idx = next_idx;
            pp->write_idx = write_idx;
            pp->stats.buffer_switch_count++;
            DebugP_log("[Core1] PP: Switch to buf[%u]\r\n", write_idx);
        } else {
            pp->stats.timeout_count++;
            return 0;
        }
    }

    /* Write states to buffer */
    for (uint16_t i = 0; i < count; i++) {
        pp->buffer[write_idx][i] = states[i];
    }

    /* Update sequence and mark as ready */
    pp->write_seq[write_idx]++;
    gateway_memory_barrier();
    pp->write_done[write_idx] = 1;
    gateway_memory_barrier();
    pp->stats.write_count++;

    return count;
}

/*==============================================================================
 * DEBUG FUNCTIONS
 *============================================================================*/

void gateway_pp_print_status(uint8_t core_id)
{
    DebugP_log("\r\n=== Core %d PP-Status ===\r\n", core_id);
    DebugP_log("CAN->ETH: wr=%u rd=%u buf0=%u buf1=%u\r\n",
               gGatewaySharedMem.can_to_eth_ppbuf.write_idx,
               gGatewaySharedMem.can_to_eth_ppbuf.read_idx,
               gGatewaySharedMem.can_to_eth_ppbuf.write_done[0],
               gGatewaySharedMem.can_to_eth_ppbuf.write_done[1]);
    DebugP_log("ETH->CAN: wr=%u rd=%u buf0=%u buf1=%u\r\n",
               gGatewaySharedMem.eth_to_can_ppbuf.write_idx,
               gGatewaySharedMem.eth_to_can_ppbuf.read_idx,
               gGatewaySharedMem.eth_to_can_ppbuf.write_done[0],
               gGatewaySharedMem.eth_to_can_ppbuf.write_done[1]);
    DebugP_log("=======================\r\n\r\n");
}

void gateway_pp_reset_stats(void)
{
    memset((void*)&gGatewaySharedMem.can_to_eth_ppbuf.stats, 0, sizeof(ppbuf_stats_t));
    memset((void*)&gGatewaySharedMem.eth_to_can_ppbuf.stats, 0, sizeof(ppbuf_stats_t));
}

#endif /* GATEWAY_USE_PINGPONG_BUFFER */

#if GATEWAY_USE_LOCKFREE_RINGBUF
/*==============================================================================
 * LOCK-FREE RING BUFFER IMPLEMENTATION
 *============================================================================*/

/**
 * @brief Get ring buffer pointer from raw memory
 *
 * [FIX] Ring buffer layout in shared memory:
 * - Gateway_RingBuf_Control_t ctrl (128 bytes, aligned)
 * - uint8_t data[GATEWAY_RINGBUF_SIZE] (actual data buffer)
 *
 * Gateway_RingBuf_t uses flexible array for data, so we need to
 * manually set the data pointer to skip the control structure.
 */
static inline Gateway_RingBuf_t* ringbuf_get_ptr(void* mem)
{
    Gateway_RingBuf_t* rb = (Gateway_RingBuf_t*)mem;
    /* Calculate data buffer pointer: skip control structure */
    /* NOTE: This modifies the flexible array member pointer */
    /* In C, we need to use a workaround since flexible arrays can't be assigned */

    /* For now, use raw memory pointer calculation in read/write functions */
    /* instead of relying on rb->data */
    return rb;
}

/**
 * @brief Initialize lock-free ring buffer
 */
int gateway_ringbuf_init(void* ringbuf, uint32_t size)
{
    if (ringbuf == NULL || (size & (size - 1)) != 0) {
        return -1;  /* Size must be power of 2 */
    }

    Gateway_RingBuf_t* rb = ringbuf_get_ptr(ringbuf);

    /* Initialize control structure */
    rb->ctrl.write_index = 0;
    rb->ctrl.read_index = 0;
    rb->size = size;
    rb->mask = size - 1;

    MEMORY_BARRIER_FULL();

    DebugP_log("[RingBuf] Initialized: size=%u, mask=0x%X\r\n", size, rb->mask);
    return 0;
}

/**
 * @brief Reset ring buffer indices
 */
void gateway_ringbuf_reset(void* ringbuf)
{
    if (ringbuf == NULL) {
        return;
    }

    Gateway_RingBuf_t* rb = ringbuf_get_ptr(ringbuf);

    rb->ctrl.write_index = 0;
    rb->ctrl.read_index = 0;

    MEMORY_BARRIER_FULL();
}

/**
 * @brief Get available space for writing (Producer)
 */
uint32_t gateway_ringbuf_get_free(void* ringbuf)
{
    if (ringbuf == NULL) {
        return 0;
    }

    Gateway_RingBuf_t* rb = ringbuf_get_ptr(ringbuf);

    /* Read current indices */
    MEMORY_BARRIER_DMB();
    uint32_t write_idx = rb->ctrl.write_index;
    uint32_t read_idx = rb->ctrl.read_index;

    /* Calculate free space */
    uint32_t free = (read_idx - write_idx - 1) & rb->mask;

    return free;
}

/**
 * @brief Get available data for reading (Consumer)
 */
uint32_t gateway_ringbuf_get_available(void* ringbuf)
{
    if (ringbuf == NULL) {
        return 0;
    }

    Gateway_RingBuf_t* rb = ringbuf_get_ptr(ringbuf);

    /* Read current indices */
    MEMORY_BARRIER_DMB();
    uint32_t write_idx = rb->ctrl.write_index;
    uint32_t read_idx = rb->ctrl.read_index;

    /* Calculate available data */
    uint32_t available = (write_idx - read_idx) & rb->mask;

    return available;
}

/**
 * @brief Non-blocking write to ring buffer (Producer)
 */
int gateway_ringbuf_write(void* ringbuf, const void* data, uint32_t size, uint32_t* bytes_written)
{
    if (ringbuf == NULL || data == NULL || size == 0) {
        return GATEWAY_RINGBUF_INVALID_PARAM;
    }

    Gateway_RingBuf_t* rb = ringbuf_get_ptr(ringbuf);

    /* Get current write position */
    MEMORY_BARRIER_DMB();
    uint32_t write_idx = rb->ctrl.write_index;
    uint32_t read_idx = rb->ctrl.read_index;

    /* Calculate available space */
    uint32_t free = (read_idx - write_idx - 1) & rb->mask;

    if (free < size) {
        /* Not enough space */
        if (bytes_written != NULL) {
            *bytes_written = 0;
        }
        return GATEWAY_RINGBUF_FULL;
    }

    /* Write data to buffer */
    /* [FIX] Calculate data buffer pointer: raw_mem + sizeof(ctrl) */
    uint8_t* raw_mem = (uint8_t*)rb;
    uint8_t* buf = raw_mem + sizeof(Gateway_RingBuf_Control_t);
    const uint8_t* src = (const uint8_t*)data;

    /* Handle wrap-around */
    uint32_t first_part = rb->size - (write_idx & rb->mask);
    if (size <= first_part) {
        /* No wrap-around */
        memcpy(&buf[write_idx & rb->mask], src, size);
    } else {
        /* Wrap-around */
        memcpy(&buf[write_idx & rb->mask], src, first_part);
        memcpy(buf, &src[first_part], size - first_part);
    }

    /* Update write index */
    MEMORY_BARRIER_DMB();
    rb->ctrl.write_index = (write_idx + size);
    MEMORY_BARRIER_DMB();

    if (bytes_written != NULL) {
        *bytes_written = size;
    }

    return GATEWAY_RINGBUF_OK;
}

/**
 * @brief Non-blocking read from ring buffer (Consumer)
 */
int gateway_ringbuf_read(void* ringbuf, void* buffer, uint32_t size, uint32_t* bytes_read)
{
    if (ringbuf == NULL || buffer == NULL || size == 0) {
        return GATEWAY_RINGBUF_INVALID_PARAM;
    }

    Gateway_RingBuf_t* rb = ringbuf_get_ptr(ringbuf);

    /* Get current read position */
    MEMORY_BARRIER_DMB();
    uint32_t write_idx = rb->ctrl.write_index;
    uint32_t read_idx = rb->ctrl.read_index;

    /* Calculate available data */
    uint32_t available = (write_idx - read_idx) & rb->mask;

    if (available < size) {
        /* Not enough data */
        if (bytes_read != NULL) {
            *bytes_read = 0;
        }
        return GATEWAY_RINGBUF_EMPTY;
    }

    /* Read data from buffer */
    /* [FIX] Calculate data buffer pointer: raw_mem + sizeof(ctrl) */
    uint8_t* raw_mem = (uint8_t*)rb;
    uint8_t* buf = raw_mem + sizeof(Gateway_RingBuf_Control_t);
    uint8_t* dst = (uint8_t*)buffer;

    /* Handle wrap-around */
    uint32_t first_part = rb->size - (read_idx & rb->mask);
    if (size <= first_part) {
        /* No wrap-around */
        memcpy(dst, &buf[read_idx & rb->mask], size);
    } else {
        /* Wrap-around */
        memcpy(dst, &buf[read_idx & rb->mask], first_part);
        memcpy(&dst[first_part], buf, size - first_part);
    }

    /* Update read index */
    MEMORY_BARRIER_DMB();
    rb->ctrl.read_index = (read_idx + size);
    MEMORY_BARRIER_DMB();

    if (bytes_read != NULL) {
        *bytes_read = size;
    }

    return GATEWAY_RINGBUF_OK;
}

/**
 * @brief Peek data without consuming (Consumer)
 */
int gateway_ringbuf_peek(void* ringbuf, void* buffer, uint32_t size, uint32_t* bytes_read)
{
    if (ringbuf == NULL || buffer == NULL || size == 0) {
        return GATEWAY_RINGBUF_INVALID_PARAM;
    }

    Gateway_RingBuf_t* rb = ringbuf_get_ptr(ringbuf);

    /* Get current read position */
    MEMORY_BARRIER_DMB();
    uint32_t write_idx = rb->ctrl.write_index;
    uint32_t read_idx = rb->ctrl.read_index;

    /* Calculate available data */
    uint32_t available = (write_idx - read_idx) & rb->mask;

    if (available < size) {
        /* Not enough data */
        if (bytes_read != NULL) {
            *bytes_read = 0;
        }
        return GATEWAY_RINGBUF_EMPTY;
    }

    /* Read data without updating index */
    /* [FIX] Calculate data buffer pointer: raw_mem + sizeof(ctrl) */
    uint8_t* raw_mem = (uint8_t*)rb;
    uint8_t* buf = raw_mem + sizeof(Gateway_RingBuf_Control_t);
    uint8_t* dst = (uint8_t*)buffer;

    /* Handle wrap-around */
    uint32_t first_part = rb->size - (read_idx & rb->mask);
    if (size <= first_part) {
        memcpy(dst, &buf[read_idx & rb->mask], size);
    } else {
        memcpy(dst, &buf[read_idx & rb->mask], first_part);
        memcpy(&dst[first_part], buf, size - first_part);
    }

    if (bytes_read != NULL) {
        *bytes_read = size;
    }

    return GATEWAY_RINGBUF_OK;
}

/**
 * @brief Skip bytes in buffer (Consumer)
 */
int gateway_ringbuf_skip(void* ringbuf, uint32_t size)
{
    if (ringbuf == NULL || size == 0) {
        return GATEWAY_RINGBUF_INVALID_PARAM;
    }

    Gateway_RingBuf_t* rb = ringbuf_get_ptr(ringbuf);

    /* Get current read position */
    MEMORY_BARRIER_DMB();
    uint32_t write_idx = rb->ctrl.write_index;
    uint32_t read_idx = rb->ctrl.read_index;

    /* Calculate available data */
    uint32_t available = (write_idx - read_idx) & rb->mask;

    if (available < size) {
        return GATEWAY_RINGBUF_EMPTY;
    }

    /* Update read index */
    MEMORY_BARRIER_DMB();
    rb->ctrl.read_index = (read_idx + size);
    MEMORY_BARRIER_DMB();

    return GATEWAY_RINGBUF_OK;
}

/*==============================================================================
 * LOCK-FREE RING BUFFER API - CORE 0
 *============================================================================*/

/**
 * @brief Initialize Core 0 ring buffers
 */
int gateway_ringbuf_core0_init(void)
{
    DebugP_log("[Core0] Initializing lock-free ring buffers...\r\n");

    /* Initialize buffer 0_to_1 (TX for Core0) */
    gateway_ringbuf_init(gGatewaySharedMem.ringbuf_0_to_1, GATEWAY_RINGBUF_0_TO_1_SIZE);

    /* Initialize buffer 1_to_0 (RX for Core0) */
    gateway_ringbuf_init(gGatewaySharedMem.ringbuf_1_to_0, GATEWAY_RINGBUF_1_TO_0_SIZE);

    return 0;
}

/**
 * @brief Send data to Core 1 (Producer for buf_0_to_1)
 */
int gateway_ringbuf_core0_send(const void* data, uint32_t size, uint32_t* bytes_written)
{
    return gateway_ringbuf_write(gGatewaySharedMem.ringbuf_0_to_1, data, size, bytes_written);
}

/**
 * @brief Receive data from Core 1 (Consumer for buf_1_to_0)
 */
int gateway_ringbuf_core0_receive(void* buffer, uint32_t size, uint32_t* bytes_read)
{
    return gateway_ringbuf_read(gGatewaySharedMem.ringbuf_1_to_0, buffer, size, bytes_read);
}

/**
 * @brief Check available space in TX buffer (buf_0_to_1)
 */
uint32_t gateway_ringbuf_core0_get_free(void)
{
    return gateway_ringbuf_get_free(gGatewaySharedMem.ringbuf_0_to_1);
}

/**
 * @brief Check available data in RX buffer (buf_1_to_0)
 */
uint32_t gateway_ringbuf_core0_get_available(void)
{
    return gateway_ringbuf_get_available(gGatewaySharedMem.ringbuf_1_to_0);
}

/**
 * @brief Trigger IPC notification to Core 1
 */
int gateway_ringbuf_core0_notify(void)
{
    return IpcNotify_sendMsg(CSL_CORE_ID_R5FSS0_1, GATEWAY_IPC_CLIENT_ID, MSG_ETH_DATA_READY, 1);
}

/*==============================================================================
 * LOCK-FREE RING BUFFER API - CORE 1
 *============================================================================*/

/**
 * @brief Initialize Core 1 ring buffers
 */
int gateway_ringbuf_core1_init(void)
{
    DebugP_log("[Core1] Initializing lock-free ring buffers...\r\n");

    /* Core 1 uses same buffers, but different access direction */
    /* No need to re-initialize, just verify */

    Gateway_RingBuf_t* rb_0_to_1 = ringbuf_get_ptr(gGatewaySharedMem.ringbuf_0_to_1);
    Gateway_RingBuf_t* rb_1_to_0 = ringbuf_get_ptr(gGatewaySharedMem.ringbuf_1_to_0);

    DebugP_log("[Core1] RingBuf 0->1: size=%u, wr=%u, rd=%u\r\n",
               rb_0_to_1->size, rb_0_to_1->ctrl.write_index, rb_0_to_1->ctrl.read_index);
    DebugP_log("[Core1] RingBuf 1->0: size=%u, wr=%u, rd=%u\r\n",
               rb_1_to_0->size, rb_1_to_0->ctrl.write_index, rb_1_to_0->ctrl.read_index);

    return 0;
}

/**
 * @brief Receive data from Core 0 (Consumer for buf_0_to_1)
 */
int gateway_ringbuf_core1_receive(void* buffer, uint32_t size, uint32_t* bytes_read)
{
    return gateway_ringbuf_read(gGatewaySharedMem.ringbuf_0_to_1, buffer, size, bytes_read);
}

/**
 * @brief Send data to Core 0 (Producer for buf_1_to_0)
 */
int gateway_ringbuf_core1_send(const void* data, uint32_t size, uint32_t* bytes_written)
{
    return gateway_ringbuf_write(gGatewaySharedMem.ringbuf_1_to_0, data, size, bytes_written);
}

/**
 * @brief Check available data in RX buffer (buf_0_to_1)
 */
uint32_t gateway_ringbuf_core1_get_available(void)
{
    return gateway_ringbuf_get_available(gGatewaySharedMem.ringbuf_0_to_1);
}

/**
 * @brief Check available space in TX buffer (buf_1_to_0)
 */
uint32_t gateway_ringbuf_core1_get_free(void)
{
    return gateway_ringbuf_get_free(gGatewaySharedMem.ringbuf_1_to_0);
}

/**
 * @brief Trigger IPC notification to Core 0
 */
int gateway_ringbuf_core1_notify(void)
{
    return IpcNotify_sendMsg(CSL_CORE_ID_R5FSS0_0, GATEWAY_IPC_CLIENT_ID, MSG_CAN_DATA_READY, 1);
}

/*==============================================================================
 * LOCK-FREE RING BUFFER TEST DATA API IMPLEMENTATION
 *============================================================================*/

/*==============================================================================
 * MOTOR CONFIGURATION SYNCHRONIZATION
 *============================================================================*/

/**
 * @brief Wait for Core 1 to populate motor configuration in shared memory
 *
 * Core 0 calls this to wait for Core 1 to initialize motor configuration.
 * Core 1 builds the motor lookup table and configuration after startup.
 *
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on timeout
 *
 * @note This function polls the motor_config_ready flag in shared memory
 * @note Core 1 sets this flag after populating motor_config[] and g_motor_lookup[]
 */
int gateway_wait_motor_config_ready(uint32_t timeout_ms)
{
    uint32_t timeout_cycles = timeout_ms * 1000;  /* Approximate: 1ms = 1000 cycles */

    DebugP_log("[Gateway] Waiting for motor config from Core 1 (timeout=%u ms)...\r\n", timeout_ms);

    /* Wait for Core 1 to set motor_config_ready flag */
    while (gGatewaySharedMem.motor_config_ready == 0 && timeout_cycles > 0) {
        /* Short delay to reduce bus contention */
        for (volatile int i = 0; i < 100; i++);

        timeout_cycles--;

        /* Check heartbeat - if Core 1 is alive, we should get config soon */
        if (timeout_cycles % 100000 == 0) {
            DebugP_log("[Gateway] Still waiting... heartbeat_r5f0_1=%u\r\n",
                       gGatewaySharedMem.heartbeat_r5f0_1);
        }
    }

    if (gGatewaySharedMem.motor_config_ready == 0) {
        DebugP_log("[Gateway] ERROR: Timeout waiting for motor configuration!\r\n");
        return -1;
    }

    gateway_memory_barrier();

    DebugP_log("[Gateway] Motor configuration ready from Core 1\r\n");
    return 0;
}

/**
 * @brief Signal that motor configuration is ready (called by Core 1)
 *
 * Core 1 calls this after populating motor_config[] and g_motor_lookup[]
 */
void gateway_signal_motor_config_ready(void)
{
    gateway_memory_barrier();
    gGatewaySharedMem.motor_config_ready = 1;
    gateway_memory_barrier();

    DebugP_log("[Gateway] Core 1: Motor configuration signaled ready\r\n");
}

/**
 * @brief Core 1: Write motor configuration to shared memory
 *
 * Core 1 calls this after building the lookup table to share config with Core 0.
 *
 * @param motor_config Local motor configuration table (23 motors)
 * @param motor_lookup Local motor lookup table (128x8)
 * @return 0 on success, -1 on error
 *
 * @note This is a CORE 1 ONLY function
 */
int gateway_write_motor_config(const SharedMotorConfig_t* motor_config,
                                const uint8_t motor_lookup[128][8])
{
    if (motor_config == NULL || motor_lookup == NULL) {
        return -1;
    }

    /* Copy motor configuration to shared memory */
    gateway_memory_barrier();
    for (uint8_t i = 0; i < VD1_NUM_MOTORS; i++) {
        gGatewaySharedMem.motor_config[i] = motor_config[i];
    }

    /* Copy motor lookup table to shared memory */
    gateway_memory_barrier();
    for (uint16_t motor_id = 0; motor_id < 128; motor_id++) {
        for (uint8_t bus = 0; bus < 8; bus++) {
            gGatewaySharedMem.motor_lookup[motor_id][bus] = motor_lookup[motor_id][bus];
        }
    }
    gateway_memory_barrier();

    DebugP_log("[Gateway] Core 1: Motor configuration written to shared memory (%u motors)\r\n",
               VD1_NUM_MOTORS);

    return 0;
}

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
const SharedMotorConfig_t* gateway_get_motor_config(uint8_t index)
{
    if (index >= VD1_NUM_MOTORS) {
        return NULL;
    }

    gateway_memory_barrier();
    return &gGatewaySharedMem.motor_config[index];
}

#endif /* GATEWAY_USE_LOCKFREE_RINGBUF */

/*==============================================================================
 * QA DEBUG COUNTERS (for AGENT_QA instrumentation)
 *============================================================================*/

/**
 * @brief Debug counters for runtime instrumentation
 *
 * [QA TRACE T009] - Task T009: Instrument shared memory counters
 *
 * These counters are placed at end of shared memory (offset 0x8000 from base 0x701D0000)
 * in the .bss.user_shared_mem_debug section for non-cacheable access.
 * All counters are accessible via JTAG for real-time debugging without stopping cores.
 *
 * Base Address: 0x701D0000 + 0x8000 = 0x701D8000
 *
 * Counter Map:
 *   Offset  Variable                Purpose                              [Trace ID]
 *   ------  --------------------    ----------------------------------  ----------
 *   0x00    dbg_ipc_send_count      IPC sends Core0→Core1                [T023]
 *   0x04    dbg_ipc_recv_count      IPC recvs Core1→Core0                [T024]
 *   0x08    dbg_can_rx_count        CAN RX frames                        [T025]
 *   0x0C    dbg_can_tx_count        CAN TX frames                        [T026]
 *   0x10    dbg_udp_rx_count        UDP RX packets                       [T027]
 *   0x14    dbg_udp_tx_count        UDP TX packets                       [T028]
 *   0x18    dbg_imu_frame_count     IMU frames                           [T029]
 *   0x1C    dbg_error_count         Total errors                         [T030]
 *   0x20    dbg_last_error_code     Last error code                      [T031]
 *   0x24    dbg_ipc_register_count  IPC registration calls                [T015]
 *
 * Usage:
 *   - Increment counters: DEBUG_COUNTER_INC(dbg_can_rx_count);
 *   - Set error: DEBUG_SET_ERROR(0x1234);
 *   - Read via JTAG: Monitor address 0x701D8000 + offset
 */

/* [QA TRACE T023] IPC send count Core0→Core1 */
volatile uint32_t dbg_ipc_send_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;

/* [QA TRACE T024] IPC receive count Core1→Core0 */
volatile uint32_t dbg_ipc_recv_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;

/* [QA TRACE T025] CAN RX frame count */
volatile uint32_t dbg_can_rx_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;

/* [QA TRACE T026] CAN TX frame count */
volatile uint32_t dbg_can_tx_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;

/* [QA TRACE T027] UDP RX packet count */
volatile uint32_t dbg_udp_rx_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;

/* [QA TRACE T028] UDP TX packet count */
volatile uint32_t dbg_udp_tx_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;

/* [QA TRACE T029] IMU frame count */
volatile uint32_t dbg_imu_frame_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;

/* [QA TRACE T030] Total error count */
volatile uint32_t dbg_error_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;

/* [QA TRACE T031] Last error code */
volatile uint32_t dbg_last_error_code __attribute__((section(".bss.user_shared_mem_debug"))) = 0;

/* [QA TRACE T015] IPC registration count */
volatile uint32_t dbg_ipc_register_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;
