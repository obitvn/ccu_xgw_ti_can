# CCS Multicore Debug Tutorial - AM263Px Cross-Cluster

## Overview

Hướng dẫn fix và cấu hình debug cho dự án TI AM263Px multicore với cross-cluster cores (r5fss0-0 + r5fss1-1).

**Problem Solved:** Core3 không halt khi debug lần thứ 2, vẫn tiếp tục running từ session trước.

---

## Table of Contents

1. [Background](#background)
2. [Understanding the Problem](#understanding-the-problem)
3. [Root Cause Analysis](#root-cause-analysis)
4. [Solution](#solution)
5. [Verification](#verification)
6. [Reference](#reference)

---

## Background

### AM263Px Core Architecture

```
AM263Px Dual R5F Clusters:
├── R5FSS0 (Cluster 0)
│   ├── r5fss0-0 → Cortex_R5_0 (Core0 - FreeRTOS)
│   └── r5fss0-1 → Cortex_R5_1 (unused)
└── R5FSS1 (Cluster 1)
    ├── r5fss1-0 → Cortex_R5_2 (unused)
    └── r5fss1-1 → Cortex_R5_3 (Core1 - NoRTOS)
```

### Project Structure

```
ccu_ti_mutilcore/
├── ccu_ti_multi_core_system/     # System project (multicore)
│   ├── system.xml                # Core-to-project mapping
│   └── .theia/launch.json        # ⚠️ CRITICAL: Debug configuration
├── ccu_ti_multi_core_freertos/   # Core0 project
│   ├── targetConfigs/AM263Px_R5F0.ccxml
│   └── .theia/launch.json
└── ccu_ti_multi_core_realtime/   # Core3 project
    ├── targetConfigs/AM263Px_R5F3.ccxml
    └── .theia/launch.json
```

---

## Understanding the Problem

### Symptoms

| Debug Session | Core0 (Cortex_R5_0) | Core3 (Cortex_R5_3) |
|--------------|-------------------|-------------------|
| **Lần 1** | ✅ Halt tại main | ✅ Halt tại main |
| **Stop** | ✅ Continue running | ✅ Continue running |
| **Lần 2** | ✅ Halt tại main | ❌ **Still running!** |

### Expected Behavior

```
Every debug session should:
1. Reset core
2. Halt core
3. Load symbols
4. Run to main
5. [User debugging...]
6. On stop: Continue running (don't halt)
7. Next debug: Repeat from step 1
```

---

## Root Cause Analysis

### Investigation Process

#### Step 1: Check CCXML Files

CCXML files looked correct:
```xml
<!-- ccu_ti_multi_core_realtime/targetConfigs/AM263Px_R5F3.ccxml -->
<cores>
    <core ID="Cortex_R5_3" isDefaultCore="true">
        <property Type="numericfield" Value="3" id="DebugInterfacePortNumber"/>
    </core>
</cores>
<startup>
    <command command="monitor reset" type="preConnect"/>
    <command command="monitor halt" type="preConnect"/>
</startup>
```

**Result:** CCXML was correct, but Core3 still not halting on re-debug.

#### Step 2: Analyze CCS Debug Flow

CCS Theia debug flow:
```
1. User clicks "Debug"
2. CCS reads .theia/launch.json          ← ⚠️ KEY FILE
3. Reads debuggerSettings for each core
4. Applies settings: ResetOnRestart, AutoResetOnConnect
5. Connects to target
6. Executes startup commands
```

#### Step 3: Compare Debugger Settings

**Found in `.theia/launch.json`:**

```json
{
  "name": "Cortex_R5_0",
  "debuggerSettings": {
    "data": "<PropertyValues>
      <property id=\"ResetOnRestart\"><curValue>1</curValue></property>      ✅
      <property id=\"AutoResetOnConnect\"><curValue>1</curValue></property>  ✅
      <property id=\"UseLegacyStopMode\"><curValue>1</curValue></property>
      ...
    </PropertyValues>"
  }
},
{
  "name": "Cortex_R5_3",
  "debuggerSettings": {
    "data": "<PropertyValues>
      <property id=\"FlashBoardType\"><curValue>LP</curValue></property>    ✅
      <property id=\"FlashPartType\"><curValue>Standard</curValue></property> ✅
      <!-- ResetOnRestart: MISSING! --> ❌
      <!-- AutoResetOnConnect: MISSING! --> ❌
    </PropertyValues>"
  }
}
```

### Root Cause

**`.theia/launch.json` thiếu critical reset config cho Cortex_R5_3**!

- Core0: Có `ResetOnRestart=1` và `AutoResetOnConnect=1` → Reset mỗi lần debug
- Core3: **Thiếu** các config này → Không reset, continue running từ session trước

---

## Solution

### Fix: Update `.theia/launch.json`

**File:** `.theia/launch.json`

**Add missing properties for Cortex_R5_3:**

```diff
{
  "name": "Cortex_R5_3",
  "debuggerSettings": {
-   "data": "<PropertyValues>
-     <property id=\"FlashBoardType\"><curValue>LP</curValue></property>
-     <property id=\"FlashPartType\"><curValue>Standard</curValue></property>
-   </PropertyValues>"
+   "data": "<PropertyValues>
+     <property id=\"ResetOnRestart\"><curValue>1</curValue></property>
+     <property id=\"AutoResetOnConnect\"><curValue>1</curValue></property>
+     <property id=\"CIOTimestamp\"><curValue>1</curValue></property>
+     <property id=\"UseLegacyStopMode\"><curValue>1</curValue></property>
+     <property id=\"AutoRunToLabelOnReset\"><curValue>0</curValue></property>
+     <property id=\"FlashBoardType\"><curValue>LP</curValue></property>
+     <property id=\"FlashPartType\"><curValue>Standard</curValue></property>
+     <property id=\"FlashFiles\"><curValue></curValue></property>
+   </PropertyValues>"
  }
}
```

### Complete Fixed Configuration

**All 3 configurations in `.theia/launch.json`:**

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "ccu_ti_multi_core_system",
      "cores": ["Cortex_R5_0", "Cortex_R5_3"],
      "connections": [...]
    },
    {
      "name": "ccu_ti_multi_core_freertos",
      "cores": ["Cortex_R5_0"],
      "connections": [
        {
          "name": "Cortex_R5_0",
          "debuggerSettings": {
            "data": "<PropertyValues>
              <property id=\"ResetOnRestart\"><curValue>1</curValue></property>
              <property id=\"AutoResetOnConnect\"><curValue>1</curValue></property>
              <property id=\"UseLegacyStopMode\"><curValue>1</curValue></property>
              <property id=\"AutoRunToLabelOnReset\"><curValue>0</curValue></property>
              <property id=\"FlashBoardType\"><curValue>LP</curValue></property>
              <property id=\"FlashPartType\"><curValue>Standard</curValue></property>
              <property id=\"FlashFiles\"><curValue></curValue></property>
            </PropertyValues>"
          }
        }
      ]
    },
    {
      "name": "ccu_ti_multi_core_realtime",
      "cores": ["Cortex_R5_3"],
      "connections": [
        {
          "name": "Cortex_R5_3",
          "debuggerSettings": {
            "data": "<PropertyValues>
              <property id=\"ResetOnRestart\"><curValue>1</curValue></property>
              <property id=\"AutoResetOnConnect\"><curValue>1</curValue></property>
              <property id=\"CIOTimestamp\"><curValue>1</curValue></property>
              <property id=\"UseLegacyStopMode\"><curValue>1</curValue></property>
              <property id=\"AutoRunToLabelOnReset\"><curValue>0</curValue></property>
              <property id=\"FlashBoardType\"><curValue>LP</curValue></property>
              <property id=\"FlashPartType\"><curValue>Standard</curValue></property>
              <property id=\"FlashFiles\"><curValue></curValue></property>
            </PropertyValues>"
          }
        }
      ]
    }
  ]
}
```

### Property Descriptions

| Property | Value | Description |
|----------|-------|-------------|
| `ResetOnRestart` | `1` | Reset core when restarting debug session |
| `AutoResetOnConnect` | `1` | Automatically reset when connecting to target |
| `CIOTimestamp` | `1` | Enable CIO (Code Instrumentation) timestamping |
| `UseLegacyStopMode` | `1` | Use legacy stop mode for better compatibility |
| `AutoRunToLabelOnReset` | `0` | Don't auto-run to label on reset |
| `FlashBoardType` | `LP` | Flash board type (Low Power) |
| `FlashPartType` | `Standard` | Flash part type |
| `FlashFiles` | `` | Flash files path (empty = none) |

---

## Verification

### Test Procedure

**1. Close CCS Theia completely**

**2. Reopen CCS Theia** (loads new `.theia/launch.json`)

**3. Test Debug Sessions:**

```
┌─────────────────────────────────────────────────────────────┐
│ TEST SEQUENCE                                               │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│ 1. Debug ccu_ti_multi_core_freertos (Core0)                │
│    └── Expected: Core0 HALT at main()                      │
│                                                             │
│ 2. Debug ccu_ti_multi_core_realtime (Core3)                │
│    └── Expected: Core3 HALT at main()                      │
│                                                             │
│ 3. Set breakpoints, step through code                       │
│                                                             │
│ 4. Stop both debug sessions (click square button)           │
│    └── Expected: Both cores CONTINUE running                │
│                                                             │
│ 5. Debug Core0 again                                        │
│    └── Expected: Core0 HALT at main()                      │
│                                                             │
│ 6. Debug Core3 again  ← ⚠️ THIS WAS FAILING BEFORE         │
│    └── Expected: Core3 HALT at main()  ✅ NOW FIXED         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Success Criteria

- ✅ Core0 halts on first debug
- ✅ Core3 halts on first debug
- ✅ Both cores continue when stopped
- ✅ Core0 halts on subsequent debug
- ✅ **Core3 halts on subsequent debug** (was failing, now fixed)

---

## Reference

### File Locations

```
Project Root: D:\VinDynamics\Project\CCU_TI_RS485_CAN\2.Firmware\ccu_ti_mutilcore\

Key Files:
├── .theia/launch.json                    # ⭐ MAIN DEBUG CONFIG
├── ccu_ti_multi_core_system/
│   ├── system.xml                         # Core mapping
│   └── .ccsproject                        # CCS project settings
├── ccu_ti_multi_core_freertos/
│   ├── targetConfigs/AM263Px_R5F0.ccxml  # Core0 target config
│   └── .ccsproject
└── ccu_ti_multi_core_realtime/
    ├── targetConfigs/AM263Px_R5F3.ccxml  # Core3 target config
    └── .ccsproject
```

### Key Properties for Cross-Cluster Debug

When setting up cross-cluster multicore debug, ensure:

1. **`.theia/launch.json`** has identical reset config for ALL cores
2. **`system.xml`** correctly maps cores to projects
3. **`.ccsproject`** points to correct CCXML file
4. **CCXML** files have `isDefaultCore="true"` for single-core projects

### Quick Checklist

```
☐ All cores have ResetOnRestart=1 in .theia/launch.json
☐ All cores have AutoResetOnConnect=1 in .theia/launch.json
☐ system.xml maps Cortex_R5_0 → freertos project
☐ system.xml maps Cortex_R5_3 → realtime project
☐ .ccsproject activeTargetConfiguration points to correct CCXML
☐ CCXML has isDefaultCore="true" for selected core
☐ CCXML startup commands: monitor reset, monitor halt
```

---

## Appendix

### SDK Reference

- **SDK Path:** `D:\VinDynamics\Project\CCU_TI_RS485_CAN\2.Firmware\draft\mcu_plus_sdk_am263px_11_01_00_19\`
- **Example:** `examples/drivers/ipc/ipc_spinlock_sharedmem/am263px-lp/`
- **Documentation:** `docs/api_guide_am263px/CCS_PROJECTS_PAGE.html`

### Related Files

- `DEBUG_CONFIG_README.md` - Debug configuration overview
- `CCS_DEBUG_SETUP_GUIDE.md` - Manual CCS setup guide
- `clear_ccs_cache.bat` - CCS cache cleanup script
- `resume_cores.bat` - Resume halted cores script

### Troubleshooting

| Problem | Solution |
|---------|----------|
| Core3 still running on re-debug | Close/reopen CCS, check `.theia/launch.json` |
| Cores halt after stop debug | Check "Halt on disconnect" = unchecked |
| Can't connect to Core3 | Check CCXML `isDefaultCore="true"` |
| Wrong core selected | Verify `deviceCore` in projectspec |

---

**Last Updated:** 2026-04-06
**Tested On:** CCS Theia 70.4.0, AM263P4, MCU+ SDK 11.01.00.19
