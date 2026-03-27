# CCU Multicore Migration Progress

**Date**: 2026-03-27
**Orchestrator**: Session with AGENT_A + AGENT_B
**Source**: `./draft/ccu_ti/` (RTOS, Core0)
**Target**: `./ccu_ti_mutilcore/` (Core0 RTOS + Core1 bare metal)

---

## Migration Order (LOW → HIGH complexity)

| ID | Module | Placement | Complexity | Status | Notes |
|----|--------|-----------|------------|--------|-------|
| 1 | yis320_protocol | CORE3 | LOW | ✅ COMPLETE | 724 lines, IPC integrated |
| 2 | imu_protocol_handler | CORE3 | LOW | ✅ COMPLETE | Wrapper only, IPC in yis320 |
| 3 | xgw_protocol | SHARED | LOW | ✅ COMPLETE | No malloc, conditional logging |
| 4 | motor_mapping | SHARED | LOW | ✅ COMPLETE | Already in both cores |
| 5 | fault_handlers | SHARED | LOW | ✅ COMPLETE | Core ID detection, enhanced malloc hook |
| 6 | imu_interface_isr | CORE3 | MEDIUM | ✅ COMPLETE | Parse-in-ISR with IPC |
| 7 | syslog | CORE0 | LOW | ✅ COMPLETE | Copied from source |
| 8 | ccu_log | CORE0 | LOW | ✅ COMPLETE | Copied from source |
| 9 | ccu_diagnostics | CORE0 | LOW | ✅ COMPLETE | Copied from source |
| 10 | extPhyMgmt | CORE0 | MEDIUM | ✅ COMPLETE | Already present in target |
| 11 | main | CORE0 | MEDIUM | ✅ COMPLETE | Already present in target |
| 12 | can_interface | CORE3 | HIGH | ✅ COMPLETE | Already migrated (8x CAN, 1000Hz) |
| 13 | ccu_xgw_gateway | CORE0 | HIGH | ✅ COMPLETE | Implementation file migrated, IPC integrated |

---

## Module Details

### ✅ Module 1: yis320_protocol
**Status**: COMPLETE
**Files Created**:
- `ccu_ti_multi_core_realtime/imu/yis320/yis320_protocol.c` (724 lines)
- `ccu_ti_multi_core_realtime/imu/yis320/yis320_protocol.h` (234 lines)

**Key Changes**:
- Removed FreeRTOS dependencies
- Added bare metal critical sections (`YIS320_ENTER_CRITICAL`/`EXIT_CRITICAL`)
- Added IPC integration: `yis320_convert_to_ipc()`, `gateway_write_imu_state()`, `gateway_notify_imu_ready()`
- Preserved linear buffer with deferred memmove (O(n))
- Max 2 frames per call throttling

**Verification**:
- Static allocation preserved (NO malloc)
- All edge cases handled (checksum, overflow, incomplete frame)
- IPC uses existing `imu_state_ipc_t` struct

### ✅ Module 2: imu_protocol_handler
**Status**: COMPLETE
**Files**: Already exist in target, bare-metal compliant
**Notes**: Wrapper abstraction layer, actual IPC in yis320_protocol.c

### ✅ Module 13: ccu_xgw_gateway
**Status**: COMPLETE
**Files Created**:
- `ccu_ti_multi_core_freertos/xgw_gateway/ccu_xgw_gateway.c` (1050 lines)
- `ccu_ti_multi_core_freertos/xgw_gateway/ccu_xgw_gateway.h` (285 lines)

**Key Changes**:
- Removed CAN TX task (moved to Core1 bare metal, 1000Hz cyclic)
- Removed motor state task (moved to Core1)
- Removed `xgw_can_rx_callback()` and `xgw_uart_rx_callback()` (handled by Core1)
- Modified UDP RX task to send motor commands via IPC: `gateway_ringbuf_core0_send()`
- Modified UDP TX task to read motor/IMU states from IPC: `gateway_read_motor_states()`, `gateway_read_imu_state()`
- Removed all direct CAN interface access and IMU interface access
- Kept all UDP/protocol processing logic intact
- Added IPC integration via `gateway_shared.h`

**Data Flow**:
1. UDP RX (port 61904) -> UDP RX Task -> Parse -> IPC -> Core1 -> CAN Bus
2. CAN RX -> Core1 -> IPC -> UDP TX Task -> UDP (port 53489)
3. IMU UART -> Core1 -> IPC -> UDP TX Task -> UDP (port 53489)

---

## IPC Messages Defined

| Message | Direction | Struct |
|---------|-----------|--------|
| MSG_CAN_DATA_READY | Core0→Core1 | can_frame_t |
| MSG_ETH_DATA_READY | Core1→Core0 | eth_frame_t |
| MSG_IMU_DATA_READY | Core1→Core0 | imu_state_ipc_t |
| MSG_HEARTBEAT | Both | heartbeat_t |

---

## Shared Memory Layout
- **Base**: 0x701D0000 (32KB non-cacheable)
- **Ring Buffer 0→1**: 8KB + 128B control (Core0 Producer → Core1 Consumer)
- **Ring Buffer 1→0**: 8KB + 128B control (Core1 Producer → Core0 Consumer)
- **Motor Config**: 23 × SharedMotorConfig_t (Core1 writes, Core0 reads)
- **Motor Lookup**: 128×8 bytes = 1KB (O(1) index lookup)
- **IMU Buffer**: imu_state_ipc_t + flags (Core1→Core0)

---

## Next Steps
1. Complete Module 3: xgw_protocol (SHARED)
2. Complete Module 4: motor_mapping (SHARED)
3. Complete Module 5: fault_handlers (SHARED)
4. Module 6: imu_interface_isr (MEDIUM - Core3 ISR-based)
