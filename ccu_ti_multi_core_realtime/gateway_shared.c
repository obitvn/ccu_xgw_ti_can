/**
 * @file gateway_shared.c
 * @brief Shared Memory IPC Implementation for CCU Multicore Gateway
 *
 * Lock-free shared memory communication between R5F0-0 and R5F0-1
 *
 * @author CCU Multicore Project
 * @date 2026-03-18
 */

#include "gateway_shared.h"
#include <kernel/dpl/DebugP.h>
#include <string.h>

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
    /* Debug: Log callback entry immediately */
    if (msg == MSG_TEST_DATA_READY) {
        DebugP_log("[Core1] gateway_core1_ipc_callback: MSG_TEST_DATA_READY received!\r\n");
    }

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

        case MSG_TEST_DATA_READY:
            /* Test data from Core 0 - read and log */
            {
                uint32_t test_data[16];
                int count = gateway_read_test_data(test_data);
                if (count > 0) {
                    DebugP_log("[Core1] *** TEST DATA RECEIVED (seq=%u): ", gGatewaySharedMem.test_data.sequence);
                    for (int i = 0; i < count && i < 8; i++) {  /* Log first 8 elements */
                        DebugP_log("%u ", test_data[i]);
                    }
                    DebugP_log("...\r\n");
                }
            }
            gGatewaySharedMem.stats.ipc_notify_count[clientId]++;
            break;

        case MSG_TEST_DATA_FROM_CORE1:
            /* Test data from Core 1 to Core 0 - not used on Core 1 */
            gGatewaySharedMem.stats.ipc_notify_count[clientId]++;
            break;

        case MSG_EMERGENCY_STOP:
            /* Emergency stop - disable all CAN buses */
            /* TODO: Implement emergency stop handler */
            gGatewaySharedMem.stats.error_count++;
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
    /* Core 0 -> Core 1: Send notification using common client ID */
    DebugP_log("[IPC] Core0 -> Core1: MSG_ETH_DATA_READY (clientId=%u)\r\n", GATEWAY_IPC_CLIENT_ID);
    int32_t status = IpcNotify_sendMsg(CSL_CORE_ID_R5FSS0_1, GATEWAY_IPC_CLIENT_ID, MSG_ETH_DATA_READY, 1);
    if (status != SystemP_SUCCESS) {
        DebugP_log("[IPC] ERROR: IpcNotify_sendMsg failed (status=%d)\r\n", status);
    }
    return status;
}

int gateway_notify_states_ready(void)
{
    /* Core 1 -> Core 0: Send notification using common client ID */
    DebugP_log("[IPC] Core1 -> Core0: MSG_CAN_DATA_READY (clientId=%u)\r\n", GATEWAY_IPC_CLIENT_ID);
    int32_t status = IpcNotify_sendMsg(CSL_CORE_ID_R5FSS0_0, GATEWAY_IPC_CLIENT_ID, MSG_CAN_DATA_READY, 1);
    if (status != SystemP_SUCCESS) {
        DebugP_log("[IPC] ERROR: IpcNotify_sendMsg failed (status=%d)\r\n", status);
    }
    return status;
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

/*==============================================================================
 * TEST DATA API (Shared Memory Testing)
 *============================================================================*/

int gateway_write_test_data(const uint32_t* data, uint16_t count)
{
    if (data == NULL || count > 16) {
        return -1;
    }

    /* Write data to shared memory test buffer */
    for (uint16_t i = 0; i < count; i++) {
        gGatewaySharedMem.test_data.data[i] = data[i];
    }

    /* Update sequence and mark as ready */
    gGatewaySharedMem.test_data.sequence++;
    gateway_memory_barrier();
    gGatewaySharedMem.test_data.ready = 1;
    gateway_memory_barrier();

    return 0;
}

int gateway_read_test_data(uint32_t* data)
{
    if (data == NULL) {
        return 0;
    }

    /* Check if data is ready */
    if (gGatewaySharedMem.test_data.ready == 0) {
        return 0;
    }

    /* Read all data from shared memory */
    for (uint16_t i = 0; i < 16; i++) {
        data[i] = gGatewaySharedMem.test_data.data[i];
    }

    /* Clear ready flag after reading */
    gGatewaySharedMem.test_data.ready = 0;
    gateway_memory_barrier();

    return 16;
}

int gateway_notify_test_data_ready(void)
{
    /* This function is called from the core that wrote test data */
    /* It should notify the OTHER core that data is ready */

    /* Since this is in Core 1's binary, we ARE Core 1, send to Core 0 */
    uint32_t remote_core_id = CSL_CORE_ID_R5FSS0_0;

    DebugP_log("[IPC] Core 1 -> Core 0: MSG_TEST_DATA_FROM_CORE1 (0x0A)\r\n");

    int32_t status = IpcNotify_sendMsg(
        remote_core_id,
        GATEWAY_IPC_CLIENT_ID,
        MSG_TEST_DATA_FROM_CORE1,  /* 0x0A for Core 1 -> Core 0 */
        1
    );

    if (status == SystemP_SUCCESS) {
        DebugP_log("[IPC] Core 1 test data notification: SUCCESS\r\n");
    } else {
        DebugP_log("[IPC] Core 1 test data notification: FAILED! status=%d\r\n", status);
    }

    return (status == SystemP_SUCCESS) ? 0 : -1;
}
