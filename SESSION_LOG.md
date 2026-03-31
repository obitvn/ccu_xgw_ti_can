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
