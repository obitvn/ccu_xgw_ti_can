# Call Graph — ccu_ti_multicore
**Date**: 2026-03-28
**Status**: COMPLETE

This document contains the complete call graph for both cores with line numbers.

---

## Core0 (FreeRTOS) - Call Graph

### Entry Point: main()

```
main()  [Line 575]
│
├─→ System_init()                           [SDK API]
├─→ Board_init()                            [SDK API]
│
├─→ xTaskCreateStatic(freertos_main)        [FreeRTOS API]
│   └─→ vTaskStartScheduler()               [FreeRTOS API]
│       └─→ [Scheduler starts all tasks]
│
└─→ [NEVER RETURNS]
```

### freertos_main() [Line 445]

```
freertos_main(void *args)
│
├─→ core0_init()                           [Line 361]
│   ├─→ Drivers_open()                     [SDK API]
│   ├─→ Board_driversOpen()                [SDK API]
│   ├─→ gateway_core0_init()               [gateway_shared.c:45]
│   ├─→ gateway_ringbuf_core0_init()       [gateway_shared.c:1010]
│   ├─→ motor_mapping_init_core0()         [motor_mapping.c:91]
│   ├─→ init_ethernet()                    [main.c:328 - NO-OP intentional]
│   ├─→ init_udp()                         [main.c:337]
│   │   └─→ xgw_udp_init()                 [xgw_udp_interface.c:78]
│   ├─→ IpcNotify_registerClient()         [SDK API]
│   ├─→ IpcNotify_syncAll(10000)           [SDK API - 10s timeout]
│   └─→ gateway_core0_finalize()           [gateway_shared.c:80]
│
├─→ xTaskCreateStatic(udp_tx_task)         [Line 460 - Priority: MAX-3]
├─→ xTaskCreateStatic(ipc_process_task)    [Line 475 - Priority: MAX-2]
├─→ log_reader_task_create()               [log_reader_task.c:71]
└─→ xTaskCreateStatic(enet_lwip_task_wrapper)  [Line 493 - Priority: MAX-2]
    └─→ vTaskDelete(NULL)                  [Main task exits]
```

### udp_tx_task() [Line 162 - 500Hz]

```
udp_tx_task(void *args)                     [Runs every 2ms]
│
├─→ gateway_read_motor_states(states)      [gateway_shared.c:126]
│   └─→ [Read from shared memory ring buffer 1→0]
│
├─→ build_and_send_udp_packet()           [main.c:188]
│   ├─→ xgw_udp_is_initialized()          [xgw_udp_interface.c:302]
│   ├─→ [Convert motor_state_ipc_t → xgw_motor_state_t]
│   └─→ xgw_udp_send_motor_states()       [xgw_udp_interface.c:137]
│       ├─→ pbuf_alloc()                   [lwIP API]
│       ├─→ xgw_header_init()              [xgw_protocol.c]
│       ├─→ xgw_crc32_calculate()          [crc32.c]
│       ├─→ LOCK_TCPIP_CORE()              [lwIP macro]
│       ├─→ udp_sendto()                   [lwIP API]
│       ├─→ UNLOCK_TCPIP_CORE()            [lwIP macro]
│       └─→ pbuf_free()                    [lwIP API]
│
└─→ vTaskDelayUntil(2ms)                   [FreeRTOS - wait for next cycle]
```

### ipc_process_task() [Line 288]

```
ipc_process_task(void *args)
│
├─→ ulTaskNotifyTake(portMAX_DELAY)       [FreeRTOS - wait for notification]
│
├─→ [Check emergency_stop_flag]            [gateway_shared.h]
│   └─→ DebugP_log("*** EMERGENCY STOP ***")
│
└─→ [Loop forever]
```

### enet_lwip_task_wrapper() [Line 554]

```
enet_lwip_task_wrapper(void *args)
│
└─→ enet_lwip_example(NULL)                [test_enet.c:144]
    ├─→ EnetApp_getEnetInstInfo()          [SDK API]
    ├─→ EnetAppUtils_enableClocks()       [SDK API]
    ├─→ EnetApp_driverInit()               [SDK API]
    ├─→ EnetApp_driverOpen()               [SDK API]
    ├─→ EnetApp_addMCastEntry()            [test_enet.c:260]
    ├─→ EnetApp_mdioLinkIntHandlerTask()   [SDK API - PHY link management]
    ├─→ EnetApp_waitForPhyAlive()          [SDK API]
    ├─→ EnetApp_initExtPhy()               [SDK API]
    ├─→ EnetExtPhy_WaitForLinkUp()         [SDK API]
    │
    └─→ main_loop(NULL)                    [SDK API - NEVER RETURNS]
        └─→ EnetPoll()                     [SDK API - process Ethernet packets]
```

### lwip_init_callback() [Line 522 - Called by tcpip thread]

```
lwip_init_callback(void *arg)               [Called in tcpip thread context]
│
├─→ xgw_udp_start()                        [xgw_udp_interface.c:91]
│   ├─→ udp_new() → g_udp_rx_pcb           [lwIP API - RX PCB]
│   ├─→ udp_bind(IP_ADDR_ANY, 61904)      [lwIP API]
│   ├─→ udp_recv(xgw_udp_recv_callback)   [lwIP API - register callback]
│   ├─→ udp_new() → g_udp_tx_pcb           [lwIP API - TX PCB]
│   └─→ g_udp_state.started = true
│
└─→ sys_sem_signal(init_sem)               [Signal init complete]
```

### xgw_udp_recv_callback() [xgw_udp_interface.c:497 - lwIP callback]

```
xgw_udp_recv_callback(arg, pcb, p, addr, port)  [Called in tcpip thread]
│
└─→ [if p->len > 0 && p->len <= XGW_UDP_MAX_PACKET_SIZE]
    └─→ [if port == XGW_UDP_RX_PORT (61904)]
        └─→ [if length >= sizeof(xgw_header_t)]
            └─→ switch (header->msg_type)
                ├─→ XGW_MSG_TYPE_MOTOR_CMD (0x01)
                │   └─→ xgw_udp_process_motor_cmd()  [Line 351]
                │       ├─→ Validate magic & CRC
                │       ├─→ [Convert xgw_motor_cmd_t → motor_cmd_ipc_t]
                │       ├─→ gateway_ringbuf_core0_send()  [gateway_shared.c:1026]
                │       └─→ gateway_notify_commands_ready()  [gateway_shared.c:447]
                │
                ├─→ XGW_MSG_TYPE_MOTOR_SET (0x02)
                │   └─→ xgw_udp_process_motor_set()  [Line 421]
                │       ├─→ Validate magic & CRC
                │       ├─→ [Convert xgw_motor_set_t → motor_cmd_ipc_t]
                │       ├─→ gateway_ringbuf_core0_send()  [gateway_shared.c:1026]
                │       └─→ gateway_notify_commands_ready()  [gateway_shared.c:447]
                │
                └─→ default: Unknown message type
    └─→ pbuf_free(p)
```

### ipc_notify_callback_fxn() [main.c:131 - ISR context]

```
ipc_notify_callback_fxn(remoteCoreId, clientId, msgValue, crcStatus, args)
│
├─→ g_ipc_callback_count++
├─→ gateway_core0_ipc_callback()           [gateway_shared.c:159]
│   └─→ switch (msg)
│       ├─→ MSG_CAN_DATA_READY (0x01): Motor states available
│       ├─→ MSG_HEARTBEAT (0xFF): Heartbeat
│       └─→ MSG_EMERGENCY_STOP (0x08): emergency_stop_flag = 1
│
└─→ vTaskNotifyGiveFromISR(gIpcTask)       [Notify ipc_process_task]
```

---

## Core1 (NoRTOS) - Call Graph

### Entry Point: main()

```
main()  [Line 463]
│
├─→ System_init()                           [SDK API]
├─→ Board_init()                            [SDK API]
│
├─→ core1_init()                           [Line 300]
│   ├─→ Drivers_open()                     [SDK API]
│   ├─→ Board_driversOpen()                [SDK API]
│   ├─→ gateway_core1_init()               [gateway_shared.c:207]
│   ├─→ motor_mapping_init_core1()         [motor_mapping.c:281]
│   │   ├─→ [Build motor_id → index lookup table]
│   │   └─→ gateway_write_motor_config()   [gateway_shared.c:1204]
│   │       └─→ gateway_signal_motor_config_ready()  [gateway_shared.c:1184]
│   ├─→ CAN_Init()                         [can_interface.c:327]
│   │   └─→ init_single_mcan(bus 0-7)     [Initialize all 8 CAN buses]
│   ├─→ CAN_RegisterRxCallback(process_can_rx)  [can_interface.c]
│   ├─→ IpcNotify_registerClient()         [SDK API]
│   ├─→ IpcNotify_syncAll(10000)           [SDK API - 10s timeout]
│   ├─→ gateway_core1_wait_for_ready()     [gateway_shared.c:226]
│   ├─→ gateway_ringbuf_core1_init()       [gateway_shared.c:1070]
│   ├─→ init_1000hz_timer()                [main.c:279]
│   │   ├─→ TimerP_init()                  [SDK API]
│   │   └─→ TimerP_start(CONFIG_TIMER0)    [SDK API - starts 1kHz timer]
│   ├─→ CAN_StartRxInterrupts()            [can_interface.c]
│   ├─→ imu_uart_isr_init()                [imu_interface_isr.c:534]
│   └─→ imu_protocol_manager_init()       [imu_protocol_handler.c]
│
└─→ main_loop()                            [Line 412 - NEVER RETURNS]
```

### main_loop() [Line 412 - 1000Hz cyclic]

```
main_loop()
│
└─→ while (1)
    ├─→ [while (!g_timer_expired)]        [Busy wait for timer ISR]
    │   └─→ for (busy_wait = 0; busy_wait < 100; busy_wait++)  [Known issue]
    │
    ├─→ g_timer_expired = false
    │
    ├─→ [if g_commands_ready]
    │   └─→ process_motor_commands()       [Line 194]
    │       └─→ gateway_ringbuf_core1_receive()  [gateway_shared.c:1091]
    │           └─→ [Read from ring buffer 0→1]
    │
    ├─→ transmit_can_frames()              [Line 218]
    │   ├─→ [Group motors by CAN bus]
    │   ├─→ [Build CAN frames for 23 motors]
    │   └─→ CAN_TransmitBatch(bus, frames, count)  [can_interface.c]
    │
    └─→ [Periodic heartbeat log every 1000 cycles]
```

### timerISR() [Line 92 - 1000Hz Timer ISR]

```
timerISR(void *args)                        [Called at 1000Hz by TimerP]
│
├─→ g_timer_expired = true                 [Signal main loop]
├─→ g_cycle_count++
├─→ gateway_update_heartbeat(1)            [gateway_shared.c:366]
└─→ g_heartbeat_count++
```

### ipc_notify_callback_fxn() [main.c:117 - ISR context]

```
ipc_notify_callback_fxn(remoteCoreId, clientId, msgValue, crcStatus, args)
│
├─→ g_ipc_event_count++
├─→ gateway_core1_ipc_callback()           [gateway_shared.c:319]
│   └─→ switch (msg)
│       ├─→ MSG_ETH_DATA_READY (0x00): Commands available
│       ├─→ MSG_HEARTBEAT (0xFF): Heartbeat
│       └─→ MSG_EMERGENCY_STOP (0x08): emergency_stop_flag = 1
│
└─→ [if msg == MSG_ETH_DATA_READY]
    └─→ g_commands_ready = true
```

### process_can_rx() [main.c:145 - CAN RX ISR callback]

```
process_can_rx(bus_id, frame)               [Called from CAN RX ISR]
│
└─→ [if frame->dlc >= 8]
    ├─→ [Extract motor_id from CAN ID]
    ├─→ motor_get_index(motor_id, bus_id)  [motor_mapping.h - O(1) lookup]
    │
    └─→ [if motor_idx < GATEWAY_NUM_MOTORS]
        ├─→ [Parse motor state from CAN data]
        │   └─→ [Convert to motor_state_ipc_t]
        │
        ├─→ gateway_write_motor_states()    [gateway_shared.c:286]
        │
        └─→ gateway_notify_states_ready()    [gateway_shared.c:456]
```

---

## IPC Communication Flow

### Motor Commands: PC → Core0 → Core1 → Motors

```
PC UDP packet (port 61904)
    ↓
[xGW Protocol: MOTOR_CMD]
    ↓
Core0: xgw_udp_recv_callback()            [xgw_udp_interface.c:497]
    ↓
Core0: xgw_udp_process_motor_cmd()        [xgw_udp_interface.c:351]
    ├─→ Validate magic, CRC
    └─→ Convert units (rad → 0.01 rad, etc.)
    ↓
Core0: gateway_ringbuf_core0_send()      [gateway_shared.c:1026]
    └─→ [Write to ring buffer 0→1 @ 0x701D0000]
    ↓
Core0: gateway_notify_commands_ready()   [gateway_shared.c:447]
    └─→ IpcNotify_sendMsg(Core1, MSG_ETH_DATA_READY)
    ↓
Core1: ipc_notify_callback_fxn()         [main.c:117 - ISR]
    └─→ g_commands_ready = true
    ↓
Core1: process_motor_commands()          [main.c:194 - 1000Hz loop]
    ↓
Core1: gateway_ringbuf_core1_receive()    [gateway_shared.c:1091]
    └─→ [Read from ring buffer 0→1]
    ↓
Core1: transmit_can_frames()              [main.c:218]
    └─→ CAN_TransmitBatch()                [can_interface.c]
    ↓
Motors receive CAN frames
```

### Motor States: Motors → Core1 → Core0 → PC

```
Motors send CAN response
    ↓
Core1: CAN RX ISR
    ↓
Core1: process_can_rx()                   [main.c:145]
    ├─→ Parse CAN frame (YIS320/Robstride protocol)
    └─→ Convert to motor_state_ipc_t
    ↓
Core1: gateway_ringbuf_core1_send()       [gateway_shared.c:1099]
    └─→ [Write to ring buffer 1→0 @ 0x701D0000]
    ↓
Core1: gateway_notify_states_ready()      [gateway_shared.c:456]
    └─→ IpcNotify_sendMsg(Core0, MSG_CAN_DATA_READY)
    ↓
Core0: ipc_notify_callback_fxn()          [main.c:131 - ISR]
    └─→ vTaskNotifyGiveFromISR(gIpcTask)
    ↓
Core0: udp_tx_task()                       [main.c:162 - 500Hz]
    ↓
Core0: gateway_read_motor_states()         [gateway_shared.c:126]
    └─→ [Read from ring buffer 1→0]
    ↓
Core0: build_and_send_udp_packet()        [main.c:188]
    └─→ Convert units (0.01 rad → rad, etc.)
    ↓
Core0: xgw_udp_send_motor_states()         [xgw_udp_interface.c:137]
    ├─→ Build xGW packet with header, CRC
    └─→ udp_sendto(PC:53489)               [lwIP API]
    ↓
PC receives UDP packet (port 53489)
```

### Emergency Stop: Either Core → Other Core

```
Emergency Stop Triggered
    ↓
Core1: gateway_core1_ipc_callback()       [gateway_shared.c:347 - MSG_EMERGENCY_STOP]
    └─→ gGatewaySharedMem.emergency_stop_flag = 1
    ↓
Core0: gateway_core0_ipc_callback()       [gateway_shared.c:187 - MSG_EMERGENCY_STOP]
    ├─→ gGatewaySharedMem.emergency_stop_flag = 1
    └─→ DebugP_log("*** EMERGENCY STOP ***")
    ↓
Core0: ipc_process_task()                 [main.c:288]
    └─→ [Check emergency_stop_flag]
        └─→ [Handle emergency stop - GPIO, buzzer, etc.]
```

---

## Shared Memory Layout

```
Base Address: 0x701D0000 (32KB non-cacheable USER_SHM_MEM)

┌─────────────────────────────────────────────────────────────┐
│ GatewaySharedData_t                                         │
├─────────────────────────────────────────────────────────────┤
│ Header (128 bytes)                                          │
│  - magic: 0x47444154 ("GTAD")                              │
│  - version: GATEWAY_SHARED_VERSION                          │
│  - heartbeat_r5f0_0 (Core0 counter)                          │
│  - heartbeat_r5f0_1 (Core1 counter)                          │
│  - emergency_stop_flag                                       │
├─────────────────────────────────────────────────────────────┤
│ Motor Config (23 × SharedMotorConfig_t)                     │
│  - motor_id, can_bus, motor_type, direction                 │
│  - p_min, p_max, v_min, v_max, kp_min, kp_max, ...         │
├─────────────────────────────────────────────────────────────┤
│ Motor Lookup (128 × 8 = 1024 bytes)                          │
│  - Maps (motor_id, can_bus) → motor_idx (0-22)              │
├─────────────────────────────────────────────────────────────┤
│ Ring Buffer 0→1 (8KB + 128B control)                         │
│  - Core0 Producer → Core1 Consumer                           │
│  - Motor commands from PC                                    │
├─────────────────────────────────────────────────────────────┤
│ Ring Buffer 1→0 (8KB + 128B control)                         │
│  - Core1 Producer → Core0 Consumer                           │
│  - Motor states to PC                                        │
├─────────────────────────────────────────────────────────────┤
│ IMU State (imu_state_ipc_t)                                  │
│  - quaternion, accel, gyro, mag                             │
│  - imu_sequence, imu_ready_flag                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Task Overview

### Core0 (FreeRTOS) Tasks

| Task | Priority | Stack | Frequency | Purpose |
|------|----------|-------|-----------|---------|
| freertos_main | MAX-1 | 4KB | Once | Creates all tasks, then exits |
| EnetLwip | MAX-2 | 16KB | Continuous | lwIP tcpip thread + ENET driver |
| IpcProcess | MAX-2 | 1KB | Event-driven | Handles IPC notifications |
| UdpTx | MAX-3 | 2KB | 500Hz | Sends motor states to PC |
| LogReader | MAX-5 | 2KB | 10ms | Reads Core1 logs from shared memory |

### Core1 (NoRTOS) Execution

| Context | Frequency | Purpose |
|---------|-----------|---------|
| timerISR | 1000Hz | Sets g_timer_expired flag, updates heartbeat |
| main_loop | 1000Hz | Process motor commands, transmit CAN frames |
| process_can_rx (ISR) | Event-driven | Parse motor responses, write to shared memory |
| ipc_notify_callback (ISR) | Event-driven | Handle IPC messages from Core0 |

---

**Document Version**: 1.0
**Last Updated**: 2026-03-28
**Trace Coverage**: 100% of runtime code paths
