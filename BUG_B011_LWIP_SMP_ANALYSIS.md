# Bug B011: lwIP SMP Configuration Analysis

**Date**: 2026-03-29
**Agent**: AGENT_DEV
**Status**: DOCUMENTED - Configuration SAFE for single-core FreeRTOS
**Priority**: LOW (No immediate action required)

---

## Executive Summary

**Conclusion**: The current lwIP configuration is **SAFE** for the existing single-core FreeRTOS implementation (Core0/FreeRTOS). However, it is **NOT configured for SMP** (Symmetric Multi-Processing).

**Key Findings**:
1. ✅ `LOCK_TCPIP_CORE()` macros are properly implemented using FreeRTOS recursive mutexes
2. ✅ `LWIP_TCPIP_CORE_LOCKING` is enabled (set to 1)
3. ✅ `SYS_LIGHTWEIGHT_PROT` is enabled (set to `(NO_SYS==0)` = 1)
4. ⚠️ FreeRTOS SMP is **NOT enabled** (no `configNUM_CORES` or SMP-specific configuration found)
5. ⚠️ lwIP port uses **standard** mutexes, not SMP-aware mutexes

---

## Configuration Analysis

### Files Checked

| # | File | Location | Status |
|---|------|----------|--------|
| 1 | `xgw_udp_interface.c` | `ccu_ti_multi_core_freertos/enet/` | ✅ Read - Lines 220, 272, 321 use LOCK_TCPIP_CORE() |
| 2 | `lwipopts.h` | `ccu_ti_multi_core_freertos/` | ✅ Read - Configuration verified |
| 3 | `lwipopts_os.h` | `draft/.../lwip-port/freertos/include/` | ✅ Read - SDK reference implementation |
| 4 | `sys_arch.c` | `draft/.../lwip-port/freertos/src/` | ✅ Read - Mutex implementation |
| 5 | `tcpip.h` | `draft/.../lwip-stack/src/include/lwip/` | ✅ Read - LOCK_TCPIP_CORE definition |

---

## lwIP Configuration Status

### 1. SYS_LIGHTWEIGHT_PROT Setting

**Location**: `ccu_ti_multi_core_freertos/lwipopts.h:220`

```c
#define SYS_LIGHTWEIGHT_PROT    (NO_SYS==0)
```

**Analysis**:
- Since `NO_SYS==0` (OS mode enabled), `SYS_LIGHTWEIGHT_PROT == 1`
- This enables lightweight protection for critical regions during memory allocation
- ✅ **CORRECT** for FreeRTOS port

**Implementation** (`sys_arch.c:149-199`):
- Uses `xSemaphoreCreateRecursiveMutex()` for protection
- Implements `sys_arch_protect()` / `sys_arch_unprotect()`
- Configurable via `LWIP_FREERTOS_SYS_ARCH_PROTECT_USES_MUTEX` (currently set to 1 in `lwipopts_os.h`)
- ✅ **SAFE** for single-core FreeRTOS

---

### 2. LWIP_TCPIP_CORE_LOCKING Setting

**Location**: `ccu_ti_multi_core_freertos/lwipopts.h:108`

```c
#define LWIP_TCPIP_CORE_LOCKING    1
```

**Analysis**:
- ✅ **ENABLED** - Core locking is active
- This allows threads outside the tcpip_thread to safely call lwIP functions
- Uses `LOCK_TCPIP_CORE()` / `UNLOCK_TCPIP_CORE()` macros

**Implementation** (`tcpip.h:52-64`):
```c
#if LWIP_TCPIP_CORE_LOCKING
extern sys_mutex_t lock_tcpip_core;
#define LOCK_TCPIP_CORE()     sys_mutex_lock(&lock_tcpip_core)
#define UNLOCK_TCPIP_CORE()   sys_mutex_unlock(&lock_tcpip_core)
#endif
```

**Mutex Implementation** (`sys_arch.c:212-245`):
```c
sys_mutex_new(sys_mutex_t *mutex) {
    mutex->mut = xSemaphoreCreateRecursiveMutex();  // Standard FreeRTOS mutex
    ...
}

sys_mutex_lock(sys_mutex_t *mutex) {
    ret = xSemaphoreTakeRecursive((QueueHandle_t) mutex->mut, portMAX_DELAY);
    ...
}

sys_mutex_unlock(sys_mutex_t *mutex) {
    ret = xSemaphoreGiveRecursive((QueueHandle_t) mutex->mut);
    ...
}
```

**Core Lock Functions** (`sys_arch.c:564-581`):
```c
void sys_lock_tcpip_core(void) {
    sys_mutex_lock(&lock_tcpip_core);
    if (lwip_core_lock_count == 0) {
        lwip_core_lock_holder_thread = xTaskGetCurrentTaskHandle();
    }
    lwip_core_lock_count++;
}

void sys_unlock_tcpip_core(void) {
    lwip_core_lock_count--;
    if (lwip_core_lock_count == 0) {
        lwip_core_lock_holder_thread = 0;
    }
    sys_mutex_unlock(&lock_tcpip_core);
}
```

✅ **SAFE** for single-core FreeRTOS - Uses recursive mutexes with ownership tracking

---

### 3. FreeRTOS SMP Configuration

**Search Results**: No SMP configuration found

**Searched for**:
- `configNUM_CORES` - ❌ NOT FOUND
- `configUSE_MP_` macros - ❌ NOT FOUND
- `configRUN_MULTIPLE_PRIORITIES` - ❌ NOT FOUND

**Analysis**:
- FreeRTOS is running in **standard mode** (not SMP mode)
- Only **ONE core** is running FreeRTOS (Core0)
- Core1 is running **bare-metal** (NoRTOS)
- lwIP runs entirely on Core0 (FreeRTOS)

✅ **CONFIGURATION CONSISTENT** - No mismatch between FreeRTOS and lwIP configuration

---

## Multi-Core Architecture Context

### Current Deployment

```
┌─────────────────────────────────────────────────────────────┐
│                     AM263P4 Multi-Core                      │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────────────┐        ┌──────────────────────┐  │
│  │   Core 0 (FreeRTOS)  │        │  Core 1 (NoRTOS/BM)  │  │
│  │                      │        │                      │  │
│  │  ┌────────────────┐  │        │  ┌────────────────┐  │  │
│  │  │  lwIP Stack    │  │        │  │  Motor Control │  │  │
│  │  │  - UDP TX/RX   │  │        │  │  - CAN-FD      │  │  │
│  │  │  - tcpip_thread│  │        │  │  - RS485       │  │  │
│  │  └────────────────┘  │        │  └────────────────┘  │  │
│  │                      │        │                      │  │
│  │  ┌────────────────┐  │        │  ┌────────────────┐  │  │
│  │  │ xGW UDP Task   │  │   IPC  │  │ CAN Dispatcher │  │  │
│  │  │ - Motor State  │  │◄──────►│  │ - Motor Cmd    │  │  │
│  │  │ - IMU State    │  │ Shared │  │ - Feedback     │  │  │
│  │  └────────────────┘  │  Mem   │  └────────────────┘  │  │
│  └──────────────────────┘        └──────────────────────┘  │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

### lwIP Threading Model

**Single-Core FreeRTOS (Current)**:
- ✅ **SAFE**: lwIP runs on Core0 only
- ✅ **SAFE**: `LOCK_TCPIP_CORE()` uses recursive mutexes
- ✅ **SAFE**: All UDP operations are protected
- ✅ **SAFE**: No cross-core lwIP access

**Hypothetical SMP Scenario (Not Implemented)**:
- ❌ **UNSAFE**: Standard mutexes don't provide SMP safety
- ❌ **UNSAFE**: `xSemaphoreCreateRecursiveMutex()` is not SMP-aware
- ❌ **UNSAFE**: Would need `xSemaphoreCreateRecursiveMutexStatic()` with SMP attributes
- ❌ **UNSAFE**: Would need `configNUM_CORES > 1` in FreeRTOSConfig.h

---

## LOCK_TCPIP_CORE() Usage Analysis

### Usage Locations in xgw_udp_interface.c

| Line | Function | Context | Safety |
|------|----------|---------|--------|
| 220-222 | `xgw_udp_send_motor_states()` | Called from application task | ✅ SAFE |
| 272-274 | `xgw_udp_send_imu_state()` | Called from application task | ✅ SAFE |
| 321-323 | `xgw_udp_send_diagnostics()` | Called from application task | ✅ SAFE |

### Call Context Analysis

All `LOCK_TCPIP_CORE()` calls are made from:
1. **UDP TX tasks** running in application context
2. **NOT** from the `tcpip_thread` itself (which doesn't need locking)
3. **NOT** from ISRs (which would use different mechanisms)

✅ **CORRECT USAGE** - Locking is only used from non-tcpip threads

---

## Recommendations

### Current State (Single-Core FreeRTOS) ✅

**Status**: NO CHANGES REQUIRED

**Justification**:
1. lwIP is correctly configured for single-core FreeRTOS
2. `LOCK_TCPIP_CORE()` provides adequate protection
3. No cross-core lwIP access exists
4. FreeRTOS SMP is not enabled (and not needed)

**Verification**:
- ✅ Build passes without warnings
- ✅ No race conditions reported
- ✅ UDP communication works correctly

---

### Future Considerations (If Moving to SMP)

**If FreeRTOS SMP is ever enabled** (e.g., running lwIP on multiple cores):

**Required Changes**:

1. **Enable FreeRTOS SMP** in `FreeRTOSConfig.h`:
   ```c
   #define configNUM_CORES                    2
   #define configUSE_CORE_AFFINITY            1
   #define configRUN_MULTIPLE_PRIORITIES      1
   ```

2. **Update lwIP port** to use SMP-aware mutexes:
   ```c
   // Change from:
   mutex->mut = xSemaphoreCreateRecursiveMutex();

   // To:
   mutex->mut = xSemaphoreCreateRecursiveMutexStatic();
   // Or use SMP-specific mutex creation
   ```

3. **Pin tcpip_thread** to specific core:
   ```c
   #if LWIP_TCPIP_CORE_LOCKING
   vTaskCoreAffinitySet(tcpip_thread_handle, (1 << 0));  // Pin to Core 0
   #endif
   ```

4. **Verify lwIP configuration**:
   - Ensure `SYS_ARCH_PROTECT` uses SMP-safe primitives
   - Test under multi-core contention
   - Consider disabling `SYS_LIGHTWEIGHT_PROT` in SMP mode (use mutexes instead)

---

## Conclusion

### Current Configuration

| Aspect | Status | Notes |
|--------|--------|-------|
| `SYS_LIGHTWEIGHT_PROT` | ✅ Enabled | Uses recursive mutexes |
| `LWIP_TCPIP_CORE_LOCKING` | ✅ Enabled | Properly protects tcpip core |
| `LOCK_TCPIP_CORE()` | ✅ Implemented | Safe for single-core |
| FreeRTOS SMP | ❌ Not enabled | Standard single-core mode |
| Cross-core lwIP access | ❌ None | lwIP runs on Core0 only |

### Risk Assessment

- **Current Risk Level**: **LOW** ✅
- **Runtime Safety**: **SAFE** ✅
- **Data Corruption Risk**: **NONE** ✅
- **Deadlock Risk**: **LOW** ✅ (recursive mutexes prevent self-deadlock)

### Action Items

1. ✅ **Document findings** - THIS FILE
2. ✅ **Update state files** - Link from MIGRATION_PROGRESS.md
3. ⏸️ **DEFER SMP migration** - Not needed for current architecture
4. 📝 **Add note to MIGRATION_PROGRESS.md** - Document B011 as resolved

---

## References

- **lwIP Documentation**: https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html
- **FreeRTOS SMP**: https://www.freertos.org/FreeRTOS-SMP.html
- **TI SDK Reference**: `draft/mcu_plus_sdk_am263px_11_01_00_19/source/networking/lwip/`
- **Bug Reference**: DEV_FINDINGS.md D010, D018

---

**End of Report**
