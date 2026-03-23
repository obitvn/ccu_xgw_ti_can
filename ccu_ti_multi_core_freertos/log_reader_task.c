/**
 * @file log_reader_task.c
 * @brief Log Reader Task for Core 0 - Reads Core 1 logs from shared memory
 *
 * This task periodically reads logs from shared memory written by Core 1
 * and outputs them via UART. Core 1 writes to shared memory using
 * DebugP_shmLogWriterPutChar(), and this task calls DebugP_shmLogRead()
 * to read and forward those logs.
 *
 * @author CCU Multicore Project
 * @date 2026-03-19
 */

#include <kernel/dpl/DebugP.h>
#include "FreeRTOS.h"
#include "task.h"

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define LOG_READER_TASK_PRI  (configMAX_PRIORITIES - 5)  /* Lower priority */
#define LOG_READER_TASK_SIZE (2048U/sizeof(configSTACK_DEPTH_TYPE))
#define LOG_READER_PERIOD_MS 10  /* Read every 10ms */

/*==============================================================================
 * GLOBAL VARIABLES
 *============================================================================*/

static StackType_t gLogReaderTaskStack[LOG_READER_TASK_SIZE] __attribute__((aligned(32)));
static StaticTask_t gLogReaderTaskObj;
TaskHandle_t gLogReaderTask = NULL;

/*==============================================================================
 * TASK IMPLEMENTATION
 *============================================================================*/

/**
 * @brief Log reader task
 *
 * Periodically reads logs from shared memory and outputs to UART.
 * Core 1 writes logs to shared memory via DebugP_shmLogWriterPutChar().
 * This task calls DebugP_shmLogRead() to read and forward those logs.
 */
static void log_reader_task(void *args)
{
    (void)args;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(LOG_READER_PERIOD_MS);

    DebugP_log("[Core0] Log Reader task started (period=%d ms)\r\n", LOG_READER_PERIOD_MS);

    while (1) {
        /* Read logs from shared memory (Core 1 logs) */
        DebugP_shmLogRead();

        /* Wait for next cycle */
        vTaskDelayUntil(&last_wake_time, period);
    }
}

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * @brief Create log reader task
 *
 * @return 0 on success, -1 on error
 */
int log_reader_task_create(void)
{
    gLogReaderTask = xTaskCreateStatic(
        log_reader_task,
        "LogReader",
        LOG_READER_TASK_SIZE,
        NULL,
        LOG_READER_TASK_PRI,
        gLogReaderTaskStack,
        &gLogReaderTaskObj
    );

    if (gLogReaderTask == NULL) {
        DebugP_log("[Core0] ERROR: Failed to create Log Reader task!\r\n");
        return -1;
    }

    return 0;
}
