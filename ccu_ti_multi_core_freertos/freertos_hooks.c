/**
 * @file freertos_hooks.c
 * @brief FreeRTOS Hook Functions for Core 0
 *
 * Required hook function for FreeRTOS heap_3.c implementation
 *
 * @author CCU Multicore Project
 * @date 2026-03-24
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
 */
void vApplicationMallocFailedHook(void)
{
    DebugP_log("[Core0] FATAL: Malloc failed! System halted.\r\n");
    taskDISABLE_INTERRUPTS();
    for(;;)
    {
        /* Halt system */
    }
}
