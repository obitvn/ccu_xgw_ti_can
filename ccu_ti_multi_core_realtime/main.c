/**
 * @file main.c
 * @brief Core 1 (NoRTOS) - CAN Real-time Control
 *
 * CCU Multicore Project - Core 1:
 * - NoRTOS (Bare-metal)
 * - 8x MCAN (CAN 0-7)
 * - 1000Hz cyclic loop for motor control
 * - IPC Producer/Consumer
 *
 * @author CCU Multicore Project
 * @date 2026-03-18
 */

#include <stdlib.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/HwiP.h>
#include <kernel/dpl/ClockP.h>
#include <drivers/ipc_notify.h>
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"
#include "gateway_shared.h"
#include "can_interface.h"
#include "motor_mapping.h"
#include "dispatcher_timer.h"

/* Core ID definitions from CSL */
#ifndef CSL_CORE_ID_R5FSS0_0
#define CSL_CORE_ID_R5FSS0_0         (0U)
#endif
#ifndef CSL_CORE_ID_R5FSS0_1
#define CSL_CORE_ID_R5FSS0_1         (1U)
#endif

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define SYSTEM_CORE_CLOCK_HZ      800000000  /* 800 MHz */
#define LOOP_FREQUENCY_HZ         1000        /* 1000 Hz */
#define LOOP_PERIOD_US           (1000000 / LOOP_FREQUENCY_HZ)  /* 1000 us */

/*==============================================================================
 * GLOBAL VARIABLES
 *============================================================================*/

/* 1000Hz Timer */
static HwiP_Object g_timer_hwi;
static volatile uint64_t g_cycle_count = 0;
static volatile bool g_timer_expired = false;

/* Motor command buffer from shared memory */
static motor_cmd_ipc_t g_motor_commands[GATEWAY_NUM_MOTORS];
static volatile bool g_commands_ready = false;

/* Motor state buffer for shared memory */
static motor_state_ipc_t g_motor_states[GATEWAY_NUM_MOTORS];

/* Heartbeat counter */
static volatile uint32_t g_heartbeat_count = 0;

/* IPC test counter - tracks test messages from Core 0 */
static volatile uint32_t g_ipc_test_rx_count = 0;

/* Test data received flag - set in ISR, checked in main loop */
static volatile bool g_test_data_received = false;
static volatile uint32_t g_test_data_values[8] = {0};

/*==============================================================================
 * FORWARD DECLARATIONS
 *============================================================================*/

static void main_loop(void);
static void timer_isr(void *args);
static void ipc_notify_callback_fxn(uint32_t remoteCoreId, uint16_t localClientId,
                                     uint32_t msgValue, int32_t crcStatus, void *args);
static int32_t init_1000hz_timer(void);
static void process_can_rx(uint8_t bus_id, const can_frame_t *frame);
static void process_motor_commands(void);
static void transmit_can_frames(void);

/*==============================================================================
 * TIMER ISR (1000Hz)
 *============================================================================*/

/**
 * @brief 1000Hz Timer ISR
 *
 * Triggers every 1ms to process motor commands and transmit CAN frames
 */
static void timer_isr(void *args)
{
    (void)args;

    /* Call dispatcher timer callback for statistics and user callback */
    disp_timer_isr_callback(args);

    /* Set flag for main loop processing */
    g_timer_expired = true;
    g_cycle_count++;

    /* Update heartbeat every cycle */
    gateway_update_heartbeat(1);
    g_heartbeat_count++;
}

/*==============================================================================
 * IPC CALLBACK
 *============================================================================*/

/**
 * @brief IPC notification callback
 *
 * Called when Core 0 sends notification
 * Signature matches IpcNotify_FxnCallback
 */
static void ipc_notify_callback_fxn(uint32_t remoteCoreId, uint16_t localClientId,
                                     uint32_t msgValue, int32_t crcStatus, void *args)
{
    /* DEBUG: Log callback entry for both ETH messages */
    if (msgValue == MSG_TEST_DATA_READY) {
        DebugP_log("[Core1] *** MSG_TEST_DATA_READY CALLBACK! rCoreId=%u ***\r\n",
                   (uint16_t)remoteCoreId);
    }

    (void)crcStatus;
    (void)args;

    /* Detailed logging for IPC communication debugging */
    const char* msg_name = "UNKNOWN";
    switch (msgValue) {
        case MSG_CAN_DATA_READY:     msg_name = "MSG_CAN_DATA_READY"; break;
        case MSG_ETH_DATA_READY:     msg_name = "MSG_ETH_DATA_READY"; break;
        case MSG_CAN_DATA_ACK:       msg_name = "MSG_CAN_DATA_ACK"; break;
        case MSG_ETH_DATA_ACK:       msg_name = "MSG_ETH_DATA_ACK"; break;
        case MSG_HEARTBEAT:          msg_name = "MSG_HEARTBEAT"; break;
        case MSG_EMERGENCY_STOP:     msg_name = "MSG_EMERGENCY_STOP"; break;
        case MSG_TEST_DATA_READY:    msg_name = "MSG_TEST_DATA_READY"; break;
        case MSG_TEST_DATA_FROM_CORE1: msg_name = "MSG_TEST_DATA_FROM_CORE1"; break;
        default:                     msg_name = "UNKNOWN"; break;
    }

    DebugP_log("[Core1] IPC RX: remoteCoreId=%u, localClientId=%u, msgValue=0x%02X (%s)\r\n",
               (uint16_t)remoteCoreId, (uint16_t)localClientId, (uint16_t)msgValue, msg_name);

    /* Track test messages from Core 0 */
    if (remoteCoreId == CSL_CORE_ID_R5FSS0_0) {
        if (msgValue == MSG_ETH_DATA_READY) {
            /* Regular test message */
            g_ipc_test_rx_count++;
        } else if (msgValue == MSG_TEST_DATA_READY) {
            /* Test data ready - copy to buffer for main loop */
            for (int i = 0; i < 8; i++) {
                g_test_data_values[i] = gGatewaySharedMem.test_data.data[i];
            }
            g_test_data_received = true;
        }
    }

    /* Call gateway shared memory callback */
    gateway_core1_ipc_callback(localClientId, (uint16_t)msgValue);

    if (msgValue == MSG_ETH_DATA_READY) {
        /* Core 0 has written new motor commands */
        g_commands_ready = true;
    }
}

/*==============================================================================
 * CAN RX CALLBACK
 *============================================================================*/

/**
 * @brief CAN RX callback
 *
 * Called from CAN RX ISR when frame is received
 */
static void process_can_rx(uint8_t bus_id, const can_frame_t *frame)
{
    /* Parse motor response from CAN frame */
    if (frame->dlc >= 8) {
        /* Extract motor ID from CAN ID (Robstride protocol) */
        uint8_t motor_id = (frame->can_id >> 8) & 0x7F;

        /* Find motor index using lookup table */
        uint8_t motor_idx = motor_get_index(motor_id, bus_id);

        if (motor_idx < GATEWAY_NUM_MOTORS) {
            /* Parse motor state from CAN data */
            motor_state_ipc_t *state = &g_motor_states[motor_idx];

            state->motor_id = motor_id;
            state->can_bus = bus_id;
            state->pattern = (frame->can_id >> 6) & 0x03;
            state->error_code = (frame->can_id >> 2) & 0x0F;

            /* Extract data bytes */
            uint16_t pos_raw = (frame->data[1] << 8) | frame->data[0];
            uint16_t vel_raw = (frame->data[3] << 8) | frame->data[2];
            int16_t torque_raw = (frame->data[5] << 8) | frame->data[4];
            int16_t temp_raw = (frame->data[7] << 8) | frame->data[6];

            /* Convert to physical units */
            state->position = pos_raw;  /* 0.01 rad */
            state->velocity = (int16_t)vel_raw;  /* 0.01 rad/s */
            state->torque = torque_raw;  /* 0.01 Nm */
            state->temperature = (int16_t)(temp_raw / 10);  /* 0.1 °C */

            /* Write motor state to shared memory */
            gateway_write_motor_states(state, 1);

            /* Notify Core 0 that new motor states are available */
            gateway_notify_states_ready();
        }
    }
}

/*==============================================================================
 * MOTOR COMMAND PROCESSING
 *============================================================================*/

/**
 * @brief Process motor commands from shared memory
 *
 * Called from 1000Hz loop to get commands from Core 0
 */
static void process_motor_commands(void)
{
    int32_t count = gateway_read_motor_commands_core1(g_motor_commands);

    if (count > 0) {
        DebugP_log("[Core1] Received %d motor commands from Core 0\r\n", count);
    }
}

/*==============================================================================
 * CAN FRAME TRANSMISSION
 *============================================================================*/

/**
 * @brief Transmit CAN frames for all motors
 *
 * Called from 1000Hz loop to send commands to all 23 motors
 */
static void transmit_can_frames(void)
{
    /* Group motors by CAN bus for batch transmission */
    can_frame_t* can_frames_ptr[NUM_CAN_BUSES];
    uint16_t frame_count[NUM_CAN_BUSES] = {0};
    static can_frame_t frame_buffers[NUM_CAN_BUSES][GATEWAY_NUM_MOTORS];

    /* Build CAN frames for each motor */
    for (uint8_t i = 0; i < GATEWAY_NUM_MOTORS; i++) {
        const motor_config_t *config = motor_get_config(i);
        const motor_cmd_ipc_t *cmd = &g_motor_commands[i];

        if (config != NULL && config->can_bus < NUM_CAN_BUSES) {
            can_frame_t *frame = &frame_buffers[config->can_bus][frame_count[config->can_bus]++];

            /* Build CAN ID (Robstride protocol) */
            frame->can_id = (COMM_TYPE_MOTION_CONTROL << 24) |
                           ((uint32_t)(cmd->torque & 0xFFFF) << 8) |
                           config->motor_id;
            frame->flags = 0x01;  /* Extended ID */

            /* Build CAN data */
            frame->dlc = 8;

            uint16_t pos_int = (uint16_t)(cmd->position & 0xFFFF);
            uint16_t vel_int = (uint16_t)(cmd->velocity & 0xFFFF);
            uint16_t kp_int = (uint16_t)(cmd->kp & 0xFFFF);
            uint16_t kd_int = (uint16_t)(cmd->kd & 0xFFFF);

            frame->data[0] = pos_int & 0xFF;
            frame->data[1] = (pos_int >> 8) & 0xFF;
            frame->data[2] = vel_int & 0xFF;
            frame->data[3] = (vel_int >> 8) & 0xFF;
            frame->data[4] = kp_int & 0xFF;
            frame->data[5] = (kp_int >> 8) & 0xFF;
            frame->data[6] = kd_int & 0xFF;
            frame->data[7] = (kd_int >> 8) & 0xFF;

            can_frames_ptr[config->can_bus] = frame_buffers[config->can_bus];
        }
    }

    /* Transmit frames on all CAN buses in parallel */
    for (uint8_t bus = 0; bus < NUM_CAN_BUSES; bus++) {
        if (frame_count[bus] > 0) {
            int32_t sent = CAN_TransmitBatch(bus, can_frames_ptr[bus], frame_count[bus]);

            if (sent > 0) {
                gateway_update_stat(1, 0);  /* Update CAN TX counter */
            }
        }
    }
}

/*==============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * @brief Initialize 1000Hz timer
 */
static int32_t init_1000hz_timer(void)
{
    HwiP_Params hwiParams;

    DebugP_log("[Core1] Initializing 1000Hz timer...\r\n");

    /* TODO: Configure hardware timer for 1000Hz operation */
    /* This requires SysConfig timer configuration */

    HwiP_Params_init(&hwiParams);
    hwiParams.callback = &timer_isr;
    hwiParams.args = NULL;

    /* TODO: Register timer ISR */
    /* int32_t status = HwiP_construct(&g_timer_hwi, &hwiParams); */

    DebugP_log("[Core1] 1000Hz timer initialized (simulated)\r\n");

    return SystemP_SUCCESS;
}

/**
 * @brief Core 1 initialization
 */
static int32_t core1_init(void)
{
    int32_t status;

    DebugP_log("\r\n");
    DebugP_log("========================================\r\n");
    DebugP_log("  Core 1 (NoRTOS) Initialization\r\n");
    DebugP_log("========================================\r\n");

    /* Open drivers */
    Drivers_open();
    status = Board_driversOpen();
    DebugP_assert(status == SystemP_SUCCESS);

    /* Initialize gateway shared memory */
    status = gateway_core1_init();
    if (status != 0) {
        DebugP_log("[Core1] ERROR: Gateway shared memory init failed!\r\n");
        return -1;
    }

    /* Initialize motor mapping */
    // motor_mapping_init();
    // DebugP_log("[Core1] Motor mapping initialized\r\n");

    /* Initialize CAN buses */
    // CAN_Init();

    /* Register CAN RX callback */
    // CAN_RegisterRxCallback(process_can_rx);

    /* Register IPC callback - BOTH cores must use the SAME client ID */
    DebugP_log("[Core1] Registering IPC callback with client ID=%u\r\n", GATEWAY_IPC_CLIENT_ID);
    status = IpcNotify_registerClient(GATEWAY_IPC_CLIENT_ID, (IpcNotify_FxnCallback)ipc_notify_callback_fxn, NULL);
    if (status != SystemP_SUCCESS) {
        DebugP_log("[Core1] ERROR: IpcNotify_registerClient failed! status=%d\r\n", status);
    } else {
        DebugP_log("[Core1] IPC callback registered successfully\r\n");
    }

    /* Wait for Core 0 to be ready */
    DebugP_log("[Core1] Waiting for Core 0 IPC sync...\r\n");
    status = IpcNotify_syncAll(10000);  /* 10 second timeout for debug */
    if (status != SystemP_SUCCESS) {
        DebugP_log("[Core1] WARNING: IpcNotify_syncAll timeout!\r\n");
    }
    
    /* Now wait for shared memory initialization to be complete */
    status = gateway_core1_wait_for_ready();
    if (status != 0) {
        DebugP_log("[Core1] ERROR: Shared memory init failed!\r\n");
        return -1;
    }

    /* Initialize 1000Hz timer */
    status = init_1000hz_timer();
    if (status != SystemP_SUCCESS) {
        DebugP_log("[Core1] WARNING: Timer init failed, using simulated timing\r\n");
    }

    /* Initialize dispatcher timer */
    status = disp_timer_init();
    if (status != SystemP_SUCCESS) {
        DebugP_log("[Core1] WARNING: Dispatcher timer init failed!\r\n");
    }

    /* Start dispatcher timer */
    status = disp_timer_start();
    if (status != SystemP_SUCCESS) {
        DebugP_log("[Core1] WARNING: Dispatcher timer start failed!\r\n");
    }

    /* Start CAN RX interrupts */
    CAN_StartRxInterrupts();

    DebugP_log("\r\n========================================\r\n");
    DebugP_log("  Core 1 Init Complete!\r\n");
    DebugP_log("========================================\r\n\r\n");

    return 0;
}

/*==============================================================================
 * MAIN LOOP
 *============================================================================*/

/**
 * @brief Core 1 main loop (1000Hz cyclic)
 *
 * This is the main control loop that runs at 1000Hz:
 * 1. Check timer flag
 * 2. Read motor commands from shared memory
 * 3. Transmit CAN frames to all 23 motors
 * 4. Process CAN RX (ISR context)
 * 5. Write motor states to shared memory
 */
static void main_loop(void)
{
    DebugP_log("[Core1] Entering main loop (1000Hz)...\r\n");

    uint64_t last_heartbeat_log = 0;
    uint32_t debug_loop_count = 0;

    DebugP_log("[Core1] Starting while(1) loop...\r\n");

    /* Simple busy wait for testing - avoid ClockP_sleep which may block */
    volatile uint32_t busy_wait;

    while (1) {
        /* Debug log first thing in loop */
        // DebugP_log("[Core1] Loop top: cycle=%llu\r\n", g_cycle_count);

        /* Simple busy wait instead of ClockP_sleep */
        for (busy_wait = 0; busy_wait < 10000; busy_wait++) {
            /* Wait approximately 1ms at 400MHz */
        }

        /* Manually set timer flag to allow processing */
        g_timer_expired = true;
        g_cycle_count++;
        gateway_update_heartbeat(1);
        g_heartbeat_count++;

        /* Debug: Log every 100 cycles to verify loop is running */
        if ((g_cycle_count % 100) == 0) {
            // DebugP_log("[Core1] Loop: cycle=%llu\r\n", g_cycle_count);
            debug_loop_count++;
        }

        /* Clear timer flag */
        g_timer_expired = false;

        /* === 1000Hz Processing Start === */

        /* 1. Process motor commands from shared memory */
        if (g_commands_ready) {
            // process_motor_commands();
            g_commands_ready = false;
        }

        /* 2. Transmit CAN frames to all motors */
        // transmit_can_frames();

        /* 2.5 Check for test data received from Core 0 */
        if (g_test_data_received) {
            DebugP_log("[Core1] *** TEST DATA from Core 0: %u %u %u %u %u %u %u %u ***\r\n",
                       g_test_data_values[0], g_test_data_values[1], g_test_data_values[2],
                       g_test_data_values[3], g_test_data_values[4], g_test_data_values[5],
                       g_test_data_values[6], g_test_data_values[7]);
            g_test_data_received = false;  /* Clear flag */
        }

        /* 3. Log heartbeat and notify Core 0 every 100 cycles (for faster testing) */
        if ((g_cycle_count - last_heartbeat_log) >= 100) {
            DebugP_log("[Core1] *** Heartbeat: cycle=%llu, hb=%u, test_rx=%u ***\r\n",
                       g_cycle_count, g_heartbeat_count, g_ipc_test_rx_count);

            /* Notify Core 0 for testing */
            DebugP_log("[Core1] About to send IPC notify to Core 0...\r\n");
            int32_t ipc_status = gateway_notify_states_ready();
            DebugP_log("[Core1] IPC notify sent! status=%d\r\n", ipc_status);

            last_heartbeat_log = g_cycle_count;
        }

        /* 3.5 Write test data to shared memory and notify Core 0 every 200 cycles */
        if ((g_cycle_count % 200) == 0) {
            /* Create test pattern from Core 1: 10, 20, 30, 40, 50, 60, 70, 80 */
            uint32_t test_pattern[] = {10, 20, 30, 40, 50, 60, 70, 80, 0, 0, 0, 0, 0, 0, 0, 0};

            /* Write test data to shared memory */
            int write_status = gateway_write_test_data(test_pattern, 8);
            if (write_status == 0) {
                DebugP_log("[Core1] SHMEM: Written test data [10,20,30,40,50,60,70,80] to shared memory\r\n");

                /* Notify Core 0 that test data is ready */
                int notify_status = gateway_notify_test_data_ready();
                if (notify_status == 0) {
                    DebugP_log("[Core1] SHMEM: Test data notification sent to Core 0\r\n");
                } else {
                    DebugP_log("[Core1] SHMEM ERROR: Failed to notify Core 0! status=%d\r\n", notify_status);
                }
            } else {
                DebugP_log("[Core1] SHMEM ERROR: Failed to write test data! status=%d\r\n", write_status);
            }
        }

        /* === 1000Hz Processing End === */

        /* Check for IPC events (non-blocking) */
        /* IPC events are handled in ISR context via ipc_notify_callback */
    }
}

/*==============================================================================
 * ENTRY POINT
 *============================================================================*/

/**
 * @brief Core 1 main entry point
 */
int main(void)
{
    /* Initialize SOC and Board */
    System_init();
    Board_init();

    /* Initialize Core 1 */
    int32_t status = core1_init();
    if (status != 0) {
        DebugP_log("[Core1] ERROR: Initialization failed!\r\n");
        return -1;
    }

    /* Start main loop (never returns) */
    main_loop();

    /* Should never reach here */
    DebugP_log("[Core1] ERROR: Main loop exited unexpectedly!\r\n");

    Board_driversClose();
    /* Drivers_close() not called to keep UART open */

    return 0;
}
