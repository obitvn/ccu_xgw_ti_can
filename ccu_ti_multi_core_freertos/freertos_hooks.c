/**
 * @file freertos_hooks.c
 * @brief FreeRTOS Hook Functions for Core 0
 *
 * Required hook function for FreeRTOS heap_3.c implementation
 *
 * [MIGRATED FROM draft/ccu_ti/fault_handlers.c:60]
 * [ENHANCED: Added register capture and BKPT for debugging]
 *
 * @author CCU Multicore Project
 * @date 2026-03-24
 * @updated 2026-03-27
 */

#include "FreeRTOS.h"
#include "task.h"
#include "kernel/dpl/DebugP.h"

/*==============================================================================
 * MALLOC FAILED HOOK (Required for heap_3.c)
 *============================================================================*/

/**
 * @brief Hook function called when malloc fails
 *
 * This is required when using heap_3.c (malloc/free based allocator)
 * Called by pvPortMalloc() when allocation fails
 *
 * [MIGRATED FROM draft/ccu_ti/fault_handlers.c:60]
 * [ENHANCED: Added LR/SP capture and BKPT instruction]
 */
void vApplicationMallocFailedHook(void)
{
    volatile uint32_t lr = 0;
    volatile uint32_t sp = 0;

    /* Get current LR and SP for debugging */
    __asm__ volatile ("MOV %0, LR" : "=r"(lr));
    __asm__ volatile ("MOV %0, SP" : "=r"(sp));

    /* Log crash state */
    DebugP_log("\r\n");
    DebugP_log("========================================\r\n");
    DebugP_log("  CRASH STATE: MALLOC FAILED\r\n");
    DebugP_log("  Core: Core0 (FreeRTOS)\r\n");
    DebugP_log("  Uptime: %u ticks\r\n", xTaskGetTickCount());
    DebugP_log("========================================\r\n");
    DebugP_log("  LR: 0x%08X (return address)\r\n", lr);
    DebugP_log("  SP: 0x%08X (stack pointer)\r\n", sp);
    DebugP_log("========================================\r\n");

    /* Breakpoint for CCS debugger - will stop here */
    __asm__ volatile ("BKPT #0");

    /* Disable interrupts and halt */
    taskDISABLE_INTERRUPTS();
    for(;;)
    {
        __asm__ volatile ("NOP");
    }
}
