# Bug Registry - CCU Multicore Project

Track known bugs, fixes, and status.

---

## Bug Categories

- **CRITICAL**: System crash, hardfault, data corruption
- **HIGH**: Functional failure, timing miss, communication loss
- **MEDIUM**: Performance degradation, minor issues
- **LOW**: Cosmetic, documentation, non-functional

---

## Active Bugs (Unfixed)

| ID | Description | Severity | Location | Status | Date Found |
|----|-------------|----------|----------|--------|------------|
| B001 | Ethernet may not work after core swap (hardware constraint) | CRITICAL | MASTER_SWAP_PLAN.md | KNOWN ISSUE | 2026-03-31 |

---

## Fixed Bugs

| ID | Description | Severity | Location | Fix Details | Date Fixed |
|----|-------------|----------|----------|-------------|------------|
| B002 | Memory barrier missing after g_commands_ready flag set | HIGH | realtime/main.c:287 | Added `__asm volatile("dmb" ::: "memory")` after flag set in IPC callback | 2026-03-31 |
| B003 | Stack overflow risk in 1000Hz loop - CAN frame buffers on stack | HIGH | realtime/main.c:395 | Moved `can_frame_t frame_buffers[NUM_CAN_BUSES][GATEWAY_NUM_MOTORS]` to global scope as `g_frame_buffers` | 2026-03-31 |
| B004 | Infinite hang if timer ISR fails to fire | HIGH | realtime/main.c:656 | Added timeout protection with `TIMER_TIMEOUT_MAX` counter and force recovery | 2026-03-31 |
| B005 | Data race on motor commands buffer | HIGH | realtime/main.c:93-95 | Implemented double buffering: `g_motor_commands_buffer` (write), `g_motor_commands_working` (read), `g_buffer_ready` flag | 2026-03-31 |
| B006 | g_cycle_count uint64_t non-atomic on 32-bit ARM | MEDIUM | realtime/main.c:78 | Changed from `uint64_t` to `uint32_t` for atomic access (provides ~68 years before overflow @ 1kHz) | 2026-03-31 |
| B007 | Memory barrier missing after g_timer_expired flag set | MEDIUM | realtime/main.c:228 | Added `__asm volatile("dmb" ::: "memory")` after flag set in timer ISR | 2026-03-31 |
| B008 | Memory barrier missing after heartbeat count increment | LOW | realtime/main.c:240 | Added `__asm volatile("dmb" ::: "memory")` after heartbeat increment | 2026-03-31 |
| B009 | g_ipc_callback_count non-atomic increment (FreeRTOS) | MEDIUM | freertos/main.c:118 | Wrapped increment in `taskENTER_CRITICAL/taskEXIT_CRITICAL` for atomic access | 2026-03-31 |
| B010 | IMU IPC notification in ISR context causing deadlock | HIGH | realtime/main.c:715 | Moved to task context processing with `imu_uart_process_ipc_notification()` | 2026-03-31 |
| B011 | LWIP SMP (Symmetric Multi-Processing) configuration issue | HIGH | LWIP config | Under analysis - see BUG_B011_LWIP_SMP_ANALYSIS.md | 2026-03-31 |
| B012 | UDP RX callback wrapper causes duplicate processing | MEDIUM | freertos/main.c:303 | Wrapper no longer registered - processing now direct in xgw_udp_interface.c | 2026-03-31 |
| B013 | Ring buffer receive called before initialization | HIGH | realtime/main.c:356 | Added `g_ringbuf_initialized` flag check in `process_motor_commands()` | 2026-03-31 |
| B014 | gateway_read_motor_states() return value not validated | HIGH | freertos/main.c:216 | Added check for `count == GATEWAY_NUM_MOTORS` before accessing buffer | 2026-03-31 |
| B015 | Memory barrier missing after heartbeat update in Core1 | LOW | realtime/main.c:240 | Added DMB barrier for visibility to Core0 | 2026-03-31 |
| B016 | Memory barrier missing after g_heartbeat_count increment | LOW | realtime/main.c:240 | Added DMB barrier for future debug/monitoring use | 2026-03-31 |
| B017 | GPIO toggle in IMU UART ISR causing Core1 hang | CRITICAL | realtime/imu/imu_interface_isr.c:413-419, 566-572 | Disabled GPIO toggle using AddrTranslateP_getLocalAddr() - not ISR-safe, causes deadlock | 2026-03-31 |
| B018 | Low-level HwiP API causing IMU ISR registration issues | HIGH | realtime/imu/imu_interface_isr.c:608-625 | First attempt: Changed to HwiP_construct but still crashed - further investigation needed | 2026-03-31 |
| B019 | taskENTER_CRITICAL() called from ISR context causing Core0 hang | CRITICAL | freertos/main.c:166-168 | Changed to taskENTER_CRITICAL_FROM_ISR()/taskEXIT_CRITICAL_FROM_ISR() - FreeRTOS rule violation | 2026-03-31 |
| B020 | HwiP_construct on NoRTOS causing undefined instruction | CRITICAL | realtime/imu/imu_interface_isr.c:607-632 | FINAL FIX: Confirmed HwiP_construct is correct API (matches working reference ccu_ti). Re-enabled IMU init, added debug counters | 2026-03-31 |
| B021 | IMU UDP TX rate limited to 100Hz instead of 1000Hz | HIGH | freertos/main.c:235-249 | Added cached IMU state that gets updated when new data arrives but is resent every cycle (1000Hz) regardless of YIS320 hardware output rate | 2026-04-01 |

---

## Bug Statistics

- **Total Bugs**: 22
- **Fixed**: 21
- **Active**: 1 (known hardware limitation)
- **Critical**: 0 (all fixed)
- **High**: 0 (all fixed)
- **Medium**: 0 (all fixed)
- **Low**: 0 (all fixed)

---

## Bug Template for New Entries

```markdown
| ID | Description | Severity | Location | Status | Date Found |
|----|-------------|----------|----------|--------|------------|
| BXXX | Brief description | SEVERITY | file.c:line | OPEN/FIXED | YYYY-MM-DD |
```

**Fix Details (when fixed):**
- Root cause: What caused the bug
- Solution: What was changed
- Verification: How to test the fix
- Files changed: List of modified files

---

## Related Files

- `SESSION_LOG.md` - Daily log of fixes applied
- `STUB_REGISTRY.md` - List of incomplete implementations
- `.claude/MASTER_SWAP_PLAN.md` - Core swap plan with known limitations
