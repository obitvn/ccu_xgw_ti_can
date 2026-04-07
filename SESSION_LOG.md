# CCU Multicore - Session Log

## 2026-04-07

### Fixes Applied

#### [B041] Missing MSG_IMU_DATA_READY IPC handler
- **File**: `gateway_shared.c`
- **Issue**: Core1 sends MSG_IMU_DATA_READY when IMU data is ready, but Core0's `gateway_core0_ipc_callback()` was missing the case handler
- **Fix**: Added `case MSG_IMU_DATA_READY` to increment `ipc_notify_count`
- **Impact**: Core0 now properly counts IMU IPC events

#### [B042] dbg_imu_frame_count not being incremented
- **File**: `ccu_ti_multi_core_realtime/imu/yis320/yis320_protocol.c`
- **Issue**: `dbg_imu_frame_count` defined in main.c but never incremented, causing debug log to always show `frames=0`
- **Fix**:
  1. Added `extern volatile uint32_t dbg_imu_frame_count;` declaration
  2. Added `dbg_imu_frame_count++;` after successful frame parse (line 657)
- **Impact**: Debug counter now accurately reflects parsed IMU frames

### Root Cause Analysis

**Why IMU data wasn't reaching PC:**
1. IMU frames were being parsed successfully (log showed gyro/quat values)
2. `gateway_write_imu_state()` was writing to shared memory
3. `gateway_notify_imu_ready()` was sending IPC notification
4. **BUT** Core0's `gateway_core0_ipc_callback()` wasn't handling `MSG_IMU_DATA_READY`
5. Without the handler, the IPC event was counted but no action was taken

**Why debug counters showed 0:**
- `dbg_imu_frame_count` was defined but never incremented
- Made it impossible to verify IMU parsing activity from debug output
- Static variable `g_frames_parsed` was incrementing but not visible in debug log

### Verification Status
- [x] Both cores build without warnings
- [x] `MSG_IMU_DATA_READY` case added to IPC callback
- [x] `dbg_imu_frame_count` now increments on frame parse
- [ ] Test on hardware - verify IMU data reaches PC

### Next Steps
1. Flash firmware to hardware
2. Verify IMU data is transmitted to PC via UDP
3. Check debug log shows non-zero `frames` and `ipc_events` counts
