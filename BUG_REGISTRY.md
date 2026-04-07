# CCU Multicore - Bug Registry

| ID | Description | Status | Fix Location | Date |
|----|-------------|--------|--------------|------|
| B001 | Timer ISR race condition | FIXED | main.c:228 | 2026-03-30 |
| B002 | Memory barrier missing | FIXED | main.c:287 | 2026-03-30 |
| B041 | Missing MSG_IMU_DATA_READY IPC handler | FIXED | gateway_shared.c:169 | 2026-04-07 |
| B042 | dbg_imu_frame_count not being incremented | FIXED | yis320_protocol.c:77,657 | 2026-04-07 |

## Bug Details

### B041: Missing MSG_IMU_DATA_READY IPC handler
**Symptom**: IMU data not reaching PC despite successful parsing

**Root Cause**: `gateway_core0_ipc_callback()` in `gateway_shared.c` missing `case MSG_IMU_DATA_READY` handler

**Fix**: Added case statement to increment `ipc_notify_count` when IMU data ready notification received

**Code**:
```c
case MSG_IMU_DATA_READY:
    gGatewaySharedMem.stats.ipc_notify_count[clientId & 0x3]++;
    break;
```

### B042: dbg_imu_frame_count not being incremented
**Symptom**: Debug log always shows `frames=0` despite successful IMU parsing

**Root Cause**: `dbg_imu_frame_count` defined in main.c but never incremented in yis320_protocol.c

**Fix**:
1. Added extern declaration in yis320_protocol.c
2. Added increment after successful frame parse

**Code**:
```c
extern volatile uint32_t dbg_imu_frame_count;  /* Line 77 */

if (parse_imu_frame(...)) {
    g_frames_parsed++;
    dbg_imu_frame_count++;  /* Line 657 */
    ...
}
```
