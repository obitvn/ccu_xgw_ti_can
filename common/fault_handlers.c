/**
 * @file fault_handlers.c
 * @brief ARM Fault Handlers for CCU Multicore Gateway (Shared Module)
 *
 * Overrides default weak fault handlers from SDK to provide:
 * - Data Abort handler (HardFault equivalent)
 * - Prefetch Abort handler
 * - Undefined instruction handler
 *
 * [MIGRATED FROM draft/ccu_ti/fault_handlers.c]
 * [PLACEMENT: SHARED - accessible by both Core0 (RTOS) and Core1 (bare metal)]
 *
 * Key changes for multicore:
 * - Added core ID detection and logging
 * - Removed malloc_failed_hook (moved to freertos_hooks.c for Core0 only)
 * - Removed dependency on ccu_diagnostics (circular dependency avoided)
 * - Made text_base address configurable via compile-time define
 *
 * Usage:
 * 1. Set breakpoints at BKPT instructions below
 * 2. When crash occurs, check LR/SP/DFAR/IFAR registers
 * 3. Use LR value to trace callstack
 *
 * @author Migrated from ccu_ti by Chu Tien Thinh
 * @date 2026-03-27
 */

/* Conditional includes for multicore support */
#ifdef CORE_BAREMETAL
    /* Core1 (bare metal): Minimal dependencies */
    #include "fault_handlers.h"
    /* No FreeRTOS on bare metal */
    #define FAULT_HANDLERS_NO_FREERTOS
#else
    /* Core0 (FreeRTOS): Full dependencies */
    #include "fault_handlers.h"
    #include "FreeRTOS.h"
    #include "task.h"
#endif

/* SDK includes (both cores) */
#include <kernel/dpl/HwiP.h>
#include <kernel/dpl/DebugP.h>
#include <string.h>

/*==============================================================================
 * CONFIGURATION
 *============================================================================*/

/* Text base address for offset calculation
 * Update this value for each build based on linker map file
 * Use: grep "__code_start" <project>.map | head -1
 *
 * [MIGRATED FROM draft/ccu_ti/fault_handlers.c:169]
 */
#ifndef FAULT_HANDLERS_TEXT_BASE
    #define FAULT_HANDLERS_TEXT_BASE  0x70100000  /* Default, override in project config */
#endif

/*==============================================================================
 * PRIVATE HELPER FUNCTIONS
 *============================================================================*/

/**
 * @brief Get current core ID
 *
 * For AM263P dual-core R5F cluster:
 * - Returns 0 for Core0 (FreeRTOS)
 * - Returns 1 for Core1 (bare metal)
 *
 * [NEW: Added for multicore support]
 */
uint32_t fault_handlers_get_core_id(void)
{
    uint32_t core_id;
    /* Read CPU ID register (CP15, c0, c0, 5) */
    __asm__ volatile ("MRC p15, 0, %0, c0, c0, 5" : "=r"(core_id));
    /* Extract MPIDR.CPUID bits [1:0] - returns 0 or 1 for dual core */
    return (core_id & 0x3);
}

/**
 * @brief Get core name string
 * [NEW: Added for multicore logging]
 */
static const char* get_core_name(void)
{
    uint32_t core_id = fault_handlers_get_core_id();
    return (core_id == 0) ? "Core0" : "Core1";
}

/**
 * @brief Get system uptime
 *
 * Core0: Use FreeRTOS tick count
 * Core1: Return 0 (no RTOS available)
 *
 * [NEW: Made core-aware]
 */
static uint32_t get_uptime_ticks(void)
{
#ifndef FAULT_HANDLERS_NO_FREERTOS
    return xTaskGetTickCount();
#else
    return 0;  /* Bare metal: no tick count available */
#endif
}

/**
 * @brief Log system state before crash
 * [MIGRATED FROM draft/ccu_ti/fault_handlers.c:37]
 */
static void log_crash_state(const char* fault_type)
{
    const char* core_name = get_core_name();
    uint32_t uptime = get_uptime_ticks();

    DebugP_log("\r\n");
    DebugP_log("========================================\r\n");
    DebugP_log("  CRASH STATE: %s\r\n", fault_type);
    DebugP_log("  Core: %s\r\n", core_name);
    DebugP_log("  Uptime: %u ticks\r\n", uptime);
    DebugP_log("========================================\r\n");
}

/*==============================================================================
 * ARM FAULT HANDLERS - OVERRIDE WEAK DEFAULTS FROM SDK
 *============================================================================*/

/**
 * @brief Data Abort handler (HardFault equivalent)
 *
 * Called on invalid memory access (NULL pointer, unaligned access, etc.)
 *
 * SDK Default: HwiP_armv7r_handlers_freertos.c:440 (weak, infinite loop)
 *
 * Important registers for debugging:
 * - DFAR: Data Fault Address Register - address that caused fault
 * - DFSR: Data Fault Status Register - fault type
 * - LR: Link Register - return address (check callstack from here)
 * - SPSR: Saved Program Status Register
 *
 * Fault types (DFSR.status):
 *   0x1: Alignment fault
 *   0x4: Instruction cache maintenance fault
 *   0x5: Translation fault
 *   0x6: Access flag fault
 *   0x7: Domain fault
 *   0x8: Permission fault
 *   0xD: Precise external abort
 *   0x15: Asynchronous external abort
 *
 * [MIGRATED FROM draft/ccu_ti/fault_handlers.c:143]
 */
void HwiP_user_data_abort_handler_c(DFSR dfsr, ADFSR adfsr,
                                     volatile uint32_t DFAR,
                                     volatile uint32_t LR,
                                     volatile uint32_t SPSR)
{
    volatile uint32_t sp = 0;
    __asm__ volatile ("MOV %0, SP" : "=r"(sp));

    /* Log crash state */
    log_crash_state("DATA ABORT (HardFault)");

    /* Log fault details */
    DebugP_log("  DFAR:     0x%08X (fault address)\r\n", DFAR);
    DebugP_log("  DFSR:     0x%08X (fault status)\r\n", *(uint32_t*)&dfsr);
    DebugP_log("  DFSR.status: %u\r\n", dfsr.status);
    DebugP_log("  DFSR.rw:     %u (0=read, 1=write)\r\n", dfsr.rw);
    DebugP_log("  ADFSR:    0x%08X\r\n", *(uint32_t*)&adfsr);
    if (adfsr.index != 0) {
        DebugP_log("  ADFSR.index:  %u\r\n", adfsr.index);
        DebugP_log("  ADFSR.side:   %u\r\n", adfsr.side_ext);
    }
    DebugP_log("  LR:       0x%08X (return address)\r\n", LR);
    DebugP_log("  SP:       0x%08X\r\n", sp);
    DebugP_log("  SPSR:     0x%08X\r\n", SPSR);

    /* Calculate approximate function location */
    uint32_t text_base = FAULT_HANDLERS_TEXT_BASE;
    uint32_t offset = LR - text_base;
    DebugP_log("  Offset:   0x%04X (from .text base 0x%08X)\r\n", offset, text_base);

    /* Breakpoint - CCS will stop here */
    /* Set breakpoint at this line for debugging */
    __asm__ volatile ("BKPT #0");

    while (1) {
        __asm__ volatile ("NOP");
    }
}

/**
 * @brief Prefetch Abort handler
 *
 * Called on instruction fetch error (invalid code address)
 *
 * SDK Default: HwiP_armv7r_handlers_freertos.c:433 (weak, infinite loop)
 *
 * Important registers:
 * - IFAR: Instruction Fault Address Register
 * - IFSR: Instruction Fault Status Register
 * - LR: Link Register (return address)
 *
 * [MIGRATED FROM draft/ccu_ti/fault_handlers.c:194]
 */
void HwiP_user_prefetch_abort_handler_c(IFSR ifsr, AIFSR aifsr,
                                         volatile uint32_t IFAR,
                                         volatile uint32_t LR,
                                         volatile uint32_t SPSR)
{
    volatile uint32_t sp = 0;
    __asm__ volatile ("MOV %0, SP" : "=r"(sp));

    DebugP_log("\r\n");
    DebugP_log("========================================\r\n");
    DebugP_log("  PREFETCH ABORT!\r\n");
    DebugP_log("  Core: %s\r\n", get_core_name());
    DebugP_log("========================================\r\n");
    DebugP_log("  IFAR:     0x%08X (instruction address)\r\n", IFAR);
    DebugP_log("  IFSR:     0x%08X\r\n", *(uint32_t*)&ifsr);
    DebugP_log("  IFSR.status: %u\r\n", ifsr.status);
    DebugP_log("  AIFSR:    0x%08X\r\n", *(uint32_t*)&aifsr);
    DebugP_log("  LR:       0x%08X\r\n", LR);
    DebugP_log("  SP:       0x%08X\r\n", sp);
    DebugP_log("  SPSR:     0x%08X\r\n", SPSR);

    __asm__ volatile ("BKPT #0");

    while (1) {
        __asm__ volatile ("NOP");
    }
}

/**
 * @brief Undefined instruction handler
 *
 * Called when CPU encounters undefined instruction
 *
 * SDK Default: HwiP_armv7r_handlers_freertos.c:427 (weak, infinite loop)
 *
 * [MIGRATED FROM draft/ccu_ti/fault_handlers.c:228]
 */
void HwiP_user_undefined_handler_c(volatile uint32_t LR, volatile uint32_t SPSR)
{
    volatile uint32_t sp = 0;
    __asm__ volatile ("MOV %0, SP" : "=r"(sp));

    DebugP_log("\r\n");
    DebugP_log("========================================\r\n");
    DebugP_log("  UNDEFINED INSTRUCTION!\r\n");
    DebugP_log("  Core: %s\r\n", get_core_name());
    DebugP_log("========================================\r\n");
    DebugP_log("  LR:   0x%08X\r\n", LR);
    DebugP_log("  SP:   0x%08X\r\n", sp);
    DebugP_log("  SPSR: 0x%08X\r\n", SPSR);

    __asm__ volatile ("BKPT #0");

    while (1) {
        __asm__ volatile ("NOP");
    }
}
