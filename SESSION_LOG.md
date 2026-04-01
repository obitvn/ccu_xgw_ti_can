# Session Log - CCU Multicore Project

Track development progress, fixes, and verification status across sessions.

---

## 2026-03-31 - Initial Setup

### Context
- Created CLAUDE.md for project guidelines
- Established reference project usage: draft/ccu_ti (READ-ONLY)
- Identified SDK: mcu_plus_sdk_am263px_11_01_00_19

### Project Status
- **Core0 (FreeRTOS)**: Ethernet + UDP, partially ported
- **Core1 (NoRTOS)**: CAN + IMU, partially ported
- **IPC**: Shared memory + lock-free ring buffer implemented
- **Core Swap**: In progress (see .claude/MASTER_SWAP_PLAN.md)

### Known Issues from MASTER_SWAP_PLAN.md
1. Ethernet hardware tied to Core0 - swap creates compatibility issue
2. 1000Hz timing may be affected after swap
3. Build system needs update for new core assignments

### Verification Status
- [ ] Both cores build without warnings
- [ ] IPC notification works between cores
- [ ] Motor commands flow: Ethernet → Core0 → Shared Memory → Core1 → CAN
- [ ] Motor states flow: CAN → Core1 → Shared Memory → Core0 → Ethernet
- [ ] IMU data transmission works

### Next Actions
1. Review current code status vs reference project
2. Identify missing ported code
3. Complete stub implementations
4. Verify build on both cores

---

## 2026-03-31 - IMU ISR Hang Fix (Core1 Freezing)

### Issues Found
- [NEW] IMU UART ISR causing Core1 to freeze
  - Severity: CRITICAL
  - Symptoms: Core1 stops responding, IMU data not received
  - Root causes identified:
    1. GPIO toggle in ISR using AddrTranslateP_getLocalAddr() - not ISR-safe
    2. Low-level HwiP API (HwiP_setVecAddr) instead of HwiP_construct
    3. Protocol handler not conflicting but redundant

### Fixes Applied
- [B017] GPIO toggle in IMU ISR causing Core1 hang
  - File changed: ccu_ti_multi_core_realtime/imu/imu_interface_isr.c
  - Lines 413-419: Disabled GPIO toggle at ISR entry
  - Lines 566-572: Disabled GPIO toggle at ISR exit
  - Reason: AddrTranslateP_getLocalAddr() and GPIO API calls use locks that cause deadlock in ISR context
  - Verify: Core1 should no longer freeze, IMU data should be received

- [B018] Low-level HwiP API causing ISR registration issues
  - File changed: ccu_ti_multi_core_realtime/imu/imu_interface_isr.c
  - Line 20: Removed include HwiP_armv7r_vim.h (no longer needed)
  - Lines 608-640: Replaced HwiP_setVecAddr/Pri/enableInt with HwiP_construct
  - Lines 732-746: Updated imu_uart_deinit() to use HwiP_destruct
  - FIX #2 (WRONG): Added HwiP_enableInt() - REVERTED, SDK doesn't call this
  - FIX #3 (WRONG): Removed hwiParams.priority - REVERTED, SDK sets this
  - FIX #4 (CORRECT): Follow SDK uart_echo_low_latency_interrupt.c exactly:
    - Set hwiParams.priority = gUartParams[CONFIG_UART5].intrPriority
    - DO NOT call HwiP_enableInt() after HwiP_construct()
  - Reason: HwiP_construct enables interrupt internally, SDK example is reference
  - Status: READY TO TEST - IMU init re-enabled in main.c

- [B019] taskENTER_CRITICAL() called from ISR context causing Core0 hang (ROOT CAUSE!)
  - File changed: ccu_ti_multi_core_freertos/main.c
  - Lines 166-168: Changed taskENTER_CRITICAL/EXIT to taskENTER_CRITICAL_FROM_ISR/EXIT_FROM_ISR
  - Root cause: ipc_notify_callback_fxn() runs in ISR context (IpcNotify_isr)
  - FreeRTOS rule violation:
    - taskENTER_CRITICAL() → ONLY from task context
    - taskENTER_CRITICAL_FROM_ISR() → from ISR context
  - Stack trace before fix:
    ```
    _DebugP_assert() at DebugP_log.c:105
    vTaskEnterCritical() at tasks.c:6941
    ipc_notify_callback_fxn() at main.c:167
    IpcNotify_isr()
    ```
  - Verify: Core0 should not hang, IPC communication should work

### Verification Status
- [x] Build core1 with 0 warnings
- [x] Build core0 with 0 warnings
- [x] System image generated successfully
- [ ] Run and verify Core1 doesn't freeze with IMU enabled
- [ ] Verify IMU data is received (check IMU frame counter in heartbeat)
- [ ] Check UART output for IMU initialization success

---

## 2026-03-31 - IMU ISR Re-enabled (FIX B020)

### Fixes Applied
- [B020] IMU ISR registration - Low-level HwiP API causing undefined instruction on NoRTOS
  - File changed: ccu_ti_multi_core_realtime/imu/imu_interface_isr.c
  - Lines 607-632: Changed from HwiP_setVecAddr/Pri/enableInt (low-level API) to HwiP_construct
  - Reference: draft/ccu_ti (working FreeRTOS) uses HwiP_construct successfully
  - Re-enabled: IMU init in main.c (removed #if 0 wrapper)
  - Reason: Low-level HwiP API (HwiP_setVecAddr) may not work properly on NoRTOS Core1
  - Debug counters: dbg_imu_uart_isr_count, dbg_imu_rx_byte_count, dbg_imu_frame_count added
  - Expected behavior: Core1 should not crash, IMU UART ISR should fire when data arrives
  - Verify: Flash and check UART output for:
    - "About to register ISR: intNum=43, priority=8"
    - "HwiP_construct returned: status=0"
    - "ISR registered successfully"
    - Heartbeat log: "IMU: isr_cnt=X, bytes=X, frames=X"

### Build Status
- Core0 (FreeRTOS): 0 warnings
- Core1 (NoRTOS): 0 warnings
- System image: ipc_spinlock_sharedmem_system.mcelf generated successfully

### Test Plan
1. Flash firmware to hardware
2. Monitor UART output for Core1 initialization
3. Verify no undefined instruction crash
4. Check heartbeat logs for IMU debug counters
5. If IMU hardware connected, verify isr_count increments when data received

---

## 2026-04-01 - IMU UDP TX Rate Fix (100Hz → 1000Hz)

### Issues Found
- PC only receives IMU data at 100Hz instead of expected 1000Hz
- Root cause: `gateway_read_imu_state()` returns -1 when `imu_ready_flag == 0`, then clears the flag
- When Core1 only updates at 100Hz (YIS320 hardware rate), Core0 only sends at 100Hz
- User requirement: Resend cached IMU data to maintain 1000Hz UDP rate regardless of YIS320 hardware output rate

### Fixes Applied
- [B021] IMU UDP TX rate limited to 100Hz due to flag-based data availability check
  - File changed: ccu_ti_multi_core_freertos/main.c
  - Lines 112-117: Added cached IMU state variables
    - `g_cached_imu_state` - stores last valid IMU data
    - `g_imu_has_valid_data` - flag indicating cache has valid data
  - Lines 235-249: Changed UDP TX task logic
    - Always read new IMU data when available (update cache)
    - Always send IMU data (new or cached) every cycle (1000Hz)
    - This ensures PC receives IMU at configured UDP rate regardless of YIS320 rate
  - Reason: YIS320 may output at 100Hz, but we resend cached data to achieve 1000Hz on PC
  - Verify: Flash and check PC IMU rate - should be ~1000Hz instead of 100Hz
  - Code snippet:
    ```c
    /* [IMU] Always send IMU state at 1000Hz - cache and resend if no new data */
    imu_state_ipc_t imu_state;
    if (gateway_read_imu_state(&imu_state) == 0) {
        /* New IMU data available - update cache */
        g_cached_imu_state = imu_state;
        g_imu_has_valid_data = true;
    }

    /* Always send IMU data (new or cached) every cycle */
    if (g_imu_has_valid_data) {
        xgw_udp_send_imu_state((xgw_imu_state_t*)&g_cached_imu_state);
    }
    ```

### Verification Status
- [x] Both cores build without warnings
- [x] System image generated successfully
- [ ] Flash firmware and verify PC receives IMU at 1000Hz
- [ ] Verify IMU data is valid (not garbage) when cached
- [ ] Check timestamp on PC - should show ~1ms intervals (1000Hz)

---

## 2026-04-01 - FreeRTOS Tick Rate Fix (ROOT CAUSE of 100Hz UDP TX)

### Issues Found
- UDP TX task still running at 100Hz despite cache mechanism
- Log output: `[Core0] UDP TX: Actual rate=100.0 Hz, avg_period=10000.00 us`
- Root cause: Missing `FreeRTOSConfig.h` → FreeRTOS using default tick rate
- Comparison with ccu_ti (working 1000Hz) revealed missing configuration file

### Fixes Applied
- [B022] Missing FreeRTOSConfig.h causing 100Hz tick rate (ROOT CAUSE!)
  - File created: ccu_ti_multi_core_freertos/FreeRTOSConfig.h
  - Key configuration:
    ```c
    #define configTICK_RATE_HZ 1000  /* 1000 Hz - 1ms per tick */
    #define configMAX_PRIORITIES 7
    #define configTOTAL_HEAP_SIZE (80 * 1024)
    #define configSUPPORT_STATIC_ALLOCATION 1
    ```
  - Verified hardware timer config matches: gClockConfig.usecPerTick = 1000us (ti_dpl_config.c)
  - Reference: draft/ccu_ti/FreeRTOSConfig.h (working reference)
  - Task priorities adjusted:
    - ENET_LWIP_TASK_PRI: configMAX_PRIORITIES - 3 (was -2)
    - UDP_TX_TASK_PRI: configMAX_PRIORITIES - 2 (was -3, higher priority now)
  - Reason: Without configTICK_RATE_HZ=1000, FreeRTOS used default (likely 100Hz = 10ms/tick)
    - pdMS_TO_TICKS(1) with 100Hz tick rate = 0 ticks (rounds down)
    - vTaskDelay(1) with 100Hz tick rate = delays 10ms instead of 1ms
  - Verify: Flash and check UART log for:
    - `[Core0] UDP TX: Actual rate=1000.0 Hz, avg_period=1000.00 us`
    - No warning from ClockP_init() about tick rate mismatch
    - PC receives IMU at ~1000Hz

### Technical Notes
- FreeRTOS tick rate determines pdMS_TO_TICKS() conversion accuracy
- Hardware timer (RTI0) runs at 25MHz with usecPerTick=1000 → 1000Hz tick rate
- ClockP_freertos_r5.c validates configTICK_RATE_HZ matches ClockP tick rate at init
- Cache mechanism (B021) still required for 1000Hz UDP when YIS320 outputs at 100Hz

### Build Status
- Core0 (FreeRTOS): 0 warnings
- Core1 (NoRTOS): 0 warnings
- System image: ipc_spinlock_sharedmem_system.mcelf generated (3.1MB)

### Test Plan
1. Flash ipc_spinlock_sharedmem_system.mcelf to hardware
2. Monitor UART output for tick rate warning (should NOT appear)
3. Check UDP TX rate log - should show 1000Hz
4. Verify PC receives IMU at ~1000Hz using ccu_ti_diag_syslog.py

### Verification Result
- **CONFIRMED**: UDP TX đạt 1000Hz sau khi loại bỏ DebugP_log() trong loop
- Stub code (simple_udp_tx_task) cũng đạt 1000Hz → FreeRTOS config OK
- Vấn đề nằm ở blocking calls trong udp_tx_task gốc

---

## 2026-04-01 - UDP TX 1000Hz Fix (DebugP_log Blocking)

### Issues Found
- UDP TX chỉ đạt 100Hz despite configTICK_RATE_HZ=1000
- Stub code (simple_udp_tx_task) đạt 1000Hz → FreeRTOS config OK
- Vấn đề nằm trong logic udp_tx_task gốc

### Root Cause Analysis
**[B023] DebugP_log() blocking trong 1000Hz loop**
- **Vấn đề**: DebugP_log() dùng UART @ 115200 baud (~14 bytes/ms) - cực chậm và BLOCKING
- **Impact**: 9 calls DebugP_log() mỗi loop 1ms → serial output thành bottleneck
- **Tại sao blocking**: UART write đợi TX buffer empty → task block hàng trăm µs mỗi log

**Các blocking points khác (ít nghi trọng hơn)**:
1. `LOCK_TCPIP_CORE()` - Mutex lock cho lwIP (2 lần/loop)
2. `pbuf_alloc()` - Memory allocation (2 lần/loop)
3. CRC32 calculation cho 23 motors (~460 bytes)

### Fixes Applied
- [B023] Remove DebugP_log() calls from 1000Hz loop
  - File changed: ccu_ti_multi_core_freertos/main.c
  - Lines 232-317: Simplified udp_tx_task() - removed all DebugP_log() from loop
  - Chỉ giữ lại 1 log/5 giây để monitoring rate
  - Reason: DebugP_log() dùng UART blocking @ 115200 baud (~14 bytes/ms)
  - Verify: UDP TX đạt 1000Hz, log: `[Core0] UDP TX: 1000.0 Hz, 1000.00 us`

### Verification Status
- [x] Stub code (simple_udp_tx_task) đạt 1000Hz → FreeRTOS OK
- [x] Normal udp_tx_task đạt 1000Hz sau khi remove DebugP_log()
- [x] Both cores build without warnings
- [x] UART output clean, minimal logging in loop

---

## Template for Future Sessions

```markdown
## YYYY-MM-DD

### Fixes Applied
- [BXXX] Description of bug fix
  - Files changed: path/to/file.c:line
  - Reason: Why this fixes the issue
  - Verify: How to test

### Issues Found
- [NEW] Description of new issue
  - Severity: HIGH/MEDIUM/LOW
  - Symptoms: What happens
  - Next action: What to do

### Verification Status
- [ ] Both cores build without warnings
- [ ] UART output shows expected behavior
- [ ] No hardfault after X minutes
- [ ] IPC communication verified
- [ ] CAN communication verified
- [ ] UDP communication verified
```

---

## Log Format Guide

When adding entries:
1. Use date format: YYYY-MM-DD
2. Group by: Fixes Applied, Issues Found, Verification Status
3. Reference bug IDs: [B001], [B002], etc.
4. Reference stub IDs: [S001], [S002], etc.
5. Always include file paths with line numbers for changes
6. Describe verification method for each fix
