# MASTER SWAP PLAN
# CCU Core-Swap Project: FreeRTOS Core0 ↔ Bare-Metal Core1

**Project:** ccu_ti_mutilcore
**Date:** 2026-03-28
**Orchestrator:** Claude AI Agent
**Status:** PHASE 1 - Planning Complete

---

## EXECUTIVE SUMMARY

This plan details the core-swap operation between two working projects on TI AM263Px:

**BEFORE SWAP:**
- `ccu_ti_multi_core_freertos` → Runs on **Core0** (R5FSS0-0) - FreeRTOS, Ethernet Gateway
- `ccu_ti_multi_core_realtime` → Runs on **Core1** (R5FSS1-1) - NoRTOS, CAN Real-time Control

**AFTER SWAP:**
- `ccu_ti_multi_core_freertos` → Runs on **Core1** (R5FSS1-1) - FreeRTOS, Ethernet Gateway
- `ccu_ti_multi_core_realtime` → Runs on **Core0** (R5FSS0-0) - NoRTOS, CAN Real-time Control

**IMPORTANT NOTE:** The ORCHESTRATOR_PROMPT mentions "Core3" but TI AM263Px only has 2 R5F cores.
- "Core3" in the prompt = Core1 (R5FSS1-1) in TI naming convention
- "Core0" remains Core0 (R5FSS0-0)

---

## CRITICAL CONSTRAINTS & HARDWARE LIMITATIONS

### 1. Ethernet Hardware Binding
- **ENET peripheral is HARDWIRED to Core0**
- ENET_CPPI_DESC memory region (0x70240000) CANNOT be moved
- After swap: Ethernet driver must still run on Core0, but we're swapping FreeRTOS to Core1

**DECISION REQUIRED:** The swap as specified creates an IMPOSSIBLE situation:
- FreeRTOS project (Ethernet) cannot move to Core1 because Ethernet hardware is on Core0
- Bare-metal project (CAN) can move to Core0 since CAN is accessible from both cores

### 2. OCRAM Memory Layout
- Core0: OCRAM at 0x70040000 (after SBL at 0x70000000)
- Core1: OCRAM at 0x70100000
- These will swap after the core-swap

### 3. IPC Client ID
- GATEWAY_IPC_CLIENT_ID = 4 must remain the same for both cores
- IpcNotify_registerClient() uses this ID

### 4. Bare-Metal Constraints
- After swap, Core0 (running bare-metal) must enforce: NO malloc, NO printf, NO OS calls

---

## REVISED SWAP STRATEGY (Recommended)

Given the hardware constraints, I recommend a PARTIAL swap:

### Option A: Swap Only Bare-Metal to Core0
- Keep FreeRTOS + Ethernet on Core0 (hardware requirement)
- Move bare-metal CAN control from Core1 to Core0
- Run FreeRTOS as the "main" scheduler with CAN in high-priority ISRs

**Pro:** Maintains Ethernet functionality
**Con:** FreeRTOS latency may affect 1000Hz CAN timing

### Option B: Full Swap with Ethernet Bridge
- Move FreeRTOS to Core1
- Move bare-metal to Core0
- Create an Ethernet bridge layer between Core1 (FreeRTOS) and Core0 (hardware)

**Pro:** Matches original swap request
**Con:** Adds complexity, may not meet timing requirements

### Option C: Original Plan (As Requested)
- Proceed with full swap despite hardware limitations
- Document that Ethernet will NOT work after swap on Core1

**DECISION:** I will proceed with **Option C** as requested by the user, but add a WARNING about Ethernet incompatibility.

---

## MASTER SWAP PLAN - STEP BY STEP

### Step 1: Update IDE Project Files (.cproject)
**Project:** Both `ccu_ti_multi_core_freertos` and `ccu_ti_multi_core_realtime`
**Agent:** Orchestrator
**Risk:** MEDIUM
**Rollback:** Git revert

**Files to modify:**
- `ccu_ti_multi_core_freertos/.cproject`
- `ccu_ti_multi_core_realtime/.cproject`
- `ccu_ti_multi_core_freertos/.project`
- `ccu_ti_multi_core_realtime/.project`
- `ccu_ti_multi_core_freertos/.ccsproject`
- `ccu_ti_multi_core_realtime/.ccsproject`

**Changes:**

**ccu_ti_multi_core_freertos/.cproject:**
- Line 24: `DEVICE_CORE_ID=Cortex_R5_0` → `Cortex_R5_3`
- Line 148: `DEVICE_CORE_ID=Cortex_R5_0` → `Cortex_R5_3`
- Line 118: `sysConfig.CONTEXT=r5fss0-0` → `r5fss1-1`
- Line 252: `sysConfig.CONTEXT=r5fss0-0` → `r5fss1-1`
- Add OS_NORTOS to defines (remove OS_FREERTOS)
- Update include paths (remove FreeRTOS, add NoRTOS paths)
- Update library references (replace freertos.lib with nortos.lib)

**ccu_ti_multi_core_realtime/.cproject:**
- Line 24: `DEVICE_CORE_ID=Cortex_R5_3` → `Cortex_R5_0`
- Line 129: `DEVICE_CORE_ID=Cortex_R5_3` → `Cortex_R5_0`
- Line 99: `sysConfig.CONTEXT=r5fss1-1` → `r5fss0-0`
- Line 204: `sysConfig.CONTEXT=r5fss1-1` → `r5fss0-0`
- Add OS_FREERTOS to defines (remove OS_NORTOS)
- Update include paths (add FreeRTOS paths)
- Update library references (replace nortos.lib with freertos.lib)

**.project files:**
- Update project names to reflect new core assignment

**.ccsproject files:**
- Update templateProperties to reference correct core

---

### Step 2: Update Linker Scripts
**Project:** Both
**Agent:** Orchestrator
**Risk:** HIGH
**Rollback:** Git revert

**Files to modify:**
- `ccu_ti_multi_core_freertos/Debug/syscfg/linker.cmd`
- `ccu_ti_multi_core_realtime/Debug/syscfg/linker.cmd`

**Changes:**

**freertos linker.cmd (will become Core1/NoRTOS):**
- `OCRAM : ORIGIN = 0x70040000` → `0x70100000`
- Remove `.bss:ENET_CPPI_DESC` section (Ethernet won't work on Core1)

**realtime linker.cmd (will become Core0/FreeRTOS):**
- `OCRAM : ORIGIN = 0x70100000` → `0x70040000`
- Add `.bss:ENET_CPPI_DESC` section for Ethernet DMA

**SBL section:**
- Update SBL reference if needed

---

### Step 3: Update Source Code - main.c
**Project:** Both
**Agent:** AGENT_A (freertos), AGENT_B (realtime)
**Risk:** HIGH
**Rollback:** Git revert

**ccu_ti_multi_core_freertos/main.c (will become Core1 NoRTOS):**

1. Update header comments:
```c
// [CORE-SWAP: Core0→Core1] Now running on Core1
 * @file main.c
 * @brief Core 1 (NoRTOS) - Bare Metal
```

2. Remove FreeRTOS includes:
```c
// [CORE-SWAP: Core0→Core1] Remove FreeRTOS
// #include "FreeRTOS.h"
// #include "task.h"
```

3. Remove FreeRTOS task creation and scheduler:
```c
// [CORE-SWAP: Core0→Core1] Replace with bare-metal main loop
int main(void)
{
    System_init();
    Board_init();

    // Initialize bare-metal drivers
    Drivers_open();
    Board_driversOpen();

    // Enter main loop (1000Hz for motor control)
    main_loop();  // Will need to be written

    return 0;
}
```

**ccu_ti_multi_core_realtime/main.c (will become Core0 FreeRTOS):**

1. Update header comments:
```c
// [CORE-SWAP: Core1→Core0] Now running on Core0
 * @file main.c
 * @brief Core 0 (FreeRTOS) - Ethernet Gateway
```

2. Add FreeRTOS includes:
```c
// [CORE-SWAP: Core1→Core0] Add FreeRTOS
#include "FreeRTOS.h"
#include "task.h"
```

3. Add FreeRTOS task creation and scheduler:
```c
// [CORE-SWAP: Core1→Core0] Replace main_loop with FreeRTOS tasks
int main(void)
{
    System_init();
    Board_init();

    // Create FreeRTOS tasks
    xTaskCreate(ethernet_task, "Eth", 4096, NULL, 3, NULL);
    xTaskCreate(udp_tx_task, "UDPTX", 2048, NULL, 2, NULL);
    xTaskCreate(ipc_task, "IPC", 1024, NULL, 2, NULL);

    // Start scheduler
    vTaskStartScheduler();

    return 0;
}
```

---

### Step 4: Update IPC Callback Functions
**Project:** Both
**Agent:** AGENT_A, AGENT_B
**Risk:** MEDIUM
**Rollback:** Git revert

**Changes:**

**freertos/main.c:**
- Update CSL_CORE_ID references
- `CSL_CORE_ID_R5FSS0_0` remains for Core0 (newly assigned)
- Update IpcNotify_syncAll timeout if needed

**realtime/main.c:**
- Update CSL_CORE_ID references
- `CSL_CORE_ID_R5FSS0_1` remains for Core1 (newly assigned)

---

### Step 5: Update Gateway Shared Memory References
**Project:** Both (gateway_shared.c)
**Agent:** Orchestrator
**Risk:** HIGH
**Rollback:** Git revert

**Files to modify:**
- `gateway_shared.c`
- Any files that reference `gGatewaySharedMem`

**Changes:**

1. Update core ID checks in gateway_shared.c
2. Update heartbeat references (r5f0_0 ↔ r5f0_1)
3. Verify memory barrier instructions work correctly on both cores

---

### Step 6: Swap Compile Flags and Defines
**Project:** Both .cproject files
**Agent:** Orchestrator
**Risk:** MEDIUM
**Rollback:** Git revert

**freertos project:**
- Remove: `OS_FREERTOS`
- Add: `OS_NORTOS`

**realtime project:**
- Remove: `OS_NORTOS`
- Add: `OS_FREERTOS`

---

### Step 7: Update Hardware Initialization
**Project:** Both
**Agent:** AGENT_A, AGENT_B
**Risk:** HIGH
**Rollback:** Git revert

**freertos (now on Core1):**
- Remove Ethernet initialization (ENET not accessible from Core1)
- Add CAN initialization for 8x MCAN buses
- Add 1000Hz timer setup

**realtime (now on Core0):**
- Remove CAN initialization (or keep for dual-access)
- Add Ethernet initialization
- Remove 1000Hz timer (use FreeRTOS timers instead)

---

### Step 8: Update SysConfig Configuration
**Project:** Both
**Agent:** Orchestrator
**Risk:** HIGH
**Rollback:** Regenerate from .sysconfig

**Changes needed:**
- Regenerate syscfg files for swapped core assignments
- Update pinmux for new core assignments
- Update interrupt routing

---

### Step 9: Verify and Test
**Project:** Both
**Agent:** Orchestrator
**Risk:** N/A
**Rollback:** N/A

**Verification steps:**
1. Build both projects - must compile with no warnings
2. Check linker map files for correct memory addresses
3. Verify IPC client ID is still 4 for both cores
4. Test communication between cores
5. Verify timing constraints met

---

## POST-SWAP VERIFICATION CHECKLIST

### Build Verification
- [ ] freertos project (now Core1) compiles without warnings
- [ ] realtime project (now Core0) compiles without warnings
- [ ] No undefined symbols in either project
- [ ] Linker map shows correct memory addresses
- [ ] Output files have different names (no conflicts)

### Functional Verification
- [ ] IPC notification works between swapped cores
- [ ] Shared memory access works correctly
- [ ] Heartbeat counters increment properly
- [ ] Motor commands flow: Ethernet → Core0 (was Core1) → CAN
- [ ] Motor states flow: CAN → Core0 (was Core1) → Ethernet
- [ ] No memory corruption in shared regions
- [ ] Lock-free ring buffers work correctly

### Timing Verification
- [ ] 1000Hz loop maintains timing on new Core0
- [ ] UDP TX maintains 500Hz on new Core1 (may fail due to Ethernet hardware!)
- [ ] IPC latency within acceptable limits
- [ ] No priority inversion issues

### Known Limitations After Swap
- [ ] **Ethernet will NOT work on Core1** - hardware is tied to Core0
- [ ] FreeRTOS on Core1 will have different OCRAM base address
- [ ] CAN timing may be affected by Core0's other activities

---

## RISK ASSESSMENT

### HIGH RISK ITEMS
1. **Ethernet incompatibility** - CPPI descriptors tied to Core0 hardware
2. **Timing-critical 1000Hz loop** - may be affected by Core0's Ethernet DMA
3. **FreeRTOS on Core1** - different OCRAM may affect task scheduling
4. **ISR vector table swap** - must be correctly re-mapped

### MEDIUM RISK ITEMS
1. **Shared memory access patterns** - may have subtle race conditions
2. **Lock-free ring buffer memory barriers** - must verify ARM barriers work
3. **Library linking** - FreeRTOS vs NoRTOS libraries must be correctly swapped

### LOW RISK ITEMS
1. **Project file renames** - cosmetic only
2. **Comment updates** - documentation only
3. **Build system updates** - straightforward

---

## CONTINGENCY PLANS

### If Ethernet Fails on Core1 (Expected):
1. Revert freertos project to Core0
2. Keep CAN/realtime on Core0 (co-locate with Ethernet)
3. Run FreeRTOS and bare-metal in shared Core0 with priority-based scheduling

### If Timing Fails on 1000Hz Loop:
1. Increase interrupt priority for timer ISR
2. Move critical sections to assembly
3. Consider DMA for CAN TX

### If Build Fails:
1. Check library linking paths
2. Verify all include paths updated
3. Regenerate syscfg files from scratch

---

## NEXT STEPS

1. **Get user confirmation** to proceed with swap (acknowledging Ethernet limitation)
2. **Execute Step 1** - Update IDE project files
3. **Execute Step 2** - Update linker scripts
4. **Execute Steps 3-8** - Update source code
5. **Execute Step 9** - Verify and test

---

**END OF MASTER SWAP PLAN**

**Ready to proceed to Phase 2: Code Execution?**
