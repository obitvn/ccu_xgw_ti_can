/**
 * @file FreeRTOSConfig.h
 * @brief FreeRTOS Configuration for Core 0 (FreeRTOS)
 *
 * CRITICAL: configTICK_RATE_HZ must be 1000 Hz for 1ms timing resolution
 * Required for 1000Hz UDP TX operation
 *
 * @author CCU Multicore Project
 * @date 2026-04-01
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ============================================================
 * CRITICAL CONFIGURATION - 1000Hz Tick Rate
 * ============================================================
 * MUST match ccu_ti configuration for compatible timing
 * configTICK_RATE_HZ = 1000 → 1 tick = 1ms
 * This allows pdMS_TO_TICKS(1) to work correctly for 1ms delays
 * ============================================================ */
#define configTICK_RATE_HZ                      1000                                    /* 1000 Hz - 1ms per tick */

/* ============================================================
 * BASIC FreeRTOS CONFIGURATION
 * ============================================================ */
#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      SystemP_CPU_CLOCK_FREQ_HZ               /* CPU frequency from SDK */
#define configMAX_PRIORITIES                    32
#define configMINIMAL_STACK_SIZE                256
#define configTOTAL_HEAP_SIZE                   (80 * 1024)                            /* 80KB heap */
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_QUEUE_SETS                    0
/* [FIX B101] Disable time slicing for deterministic 1000Hz timing
 * Time slicing causes round-robin scheduling between equal-priority tasks,
 * introducing unpredictable jitter in UDP TX task.
 * With time slicing disabled, tasks run until they block or yield,
 * giving precise control over timing via vTaskDelayUntil(). */
#define configUSE_TIME_SLICING                  0

/* ============================================================
 * MEMORY ALLOCATION
 * ============================================================ */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION         1

/* ============================================================
 * ASSERT CONFIGURATION
 * ============================================================ */
#define configASSERT(x) if((x) == 0) {taskDISABLE_INTERRUPTS(); for(;;);}

/* ============================================================
 * HOOK CONFIGURATION
 * ============================================================ */
#define configUSE_APPLICATION_TASK_TAG          0
#define configUSE_TRACE_FACILITY                0

/* ============================================================
 * TIMING CONFIGURATION
 * ============================================================ */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            256

/* ============================================================
 * INTERRUPT CONFIGURATION
 * ============================================================ */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 7
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY)

/* ============================================================
 * lwIP INTEGRATION
 * ============================================================ */
#define configINCLUDE_vTaskPrioritySet          1
#define configINCLUDE_vTaskSuspend              1

#endif /* FREERTOS_CONFIG_H */
