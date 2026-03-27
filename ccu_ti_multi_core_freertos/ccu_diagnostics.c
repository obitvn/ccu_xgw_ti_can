/**
 * @file ccu_diagnostics.c
 * @brief Runtime diagnostics and monitoring for ccu_ti
 *
 * Provides:
 * - Stack usage monitoring
 * - Heap usage tracking
 * - Ethernet packet pool statistics
 * - CAN bus statistics
 * - Periodic health reporting
 *
 * @author ccu_ti
 * @date 2025-01-19
 */

#include "ccu_diagnostics.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "xgw_gateway/ccu_xgw_gateway.h"
#include "kernel/dpl/DebugP.h"
#include <string.h>

/* FIX 2: Heap API functions from FreeRTOS
 * These functions are defined in the FreeRTOS portable layer
 * and may require specific configuration to be available.
 */
#if configSUPPORT_DYNAMIC_ALLOCATION
/* Use FreeRTOS API directly - functions should be available via FreeRTOS.h */
#else
/* If static allocation only, heap functions may not be available */
#warning "Dynamic allocation disabled - heap diagnostics may not work"
#endif

/*==============================================================================
 * DIAGNOSTICS DATA
 *============================================================================*/

static ccu_diagnostics_t g_diagnostics = {0};
static TaskHandle_t g_diag_task_handle = NULL;
static SemaphoreHandle_t g_diag_mutex = NULL;

/*==============================================================================
 * PRIVATE FUNCTIONS
 *============================================================================*/

/**
 * @brief Check stack high water mark for a task
 */
static uint32_t get_stack_hwm(const char* task_name)
{
    TaskHandle_t task = NULL;
    eTaskState state;

    /* Find task by name */
    TaskStatus_t* pxTaskStatusArray;
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
    uint32_t hwm = 0;

    pxTaskStatusArray = pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
    if (pxTaskStatusArray != NULL) {
        /* Get system state - third param is optional task counter output */
        UBaseType_t task_counter = 0;
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &task_counter);
        for (UBaseType_t x = 0; x < uxArraySize; x++) {
            if (strcmp(pxTaskStatusArray[x].pcTaskName, task_name) == 0) {
                hwm = pxTaskStatusArray[x].usStackHighWaterMark;
                break;
            }
        }
        vPortFree(pxTaskStatusArray);
    }

    return hwm;
}

/**
 * @brief Update all diagnostics
 */
static void update_diagnostics(void)
{
    if (g_diag_mutex != NULL) {
        if (xSemaphoreTake(g_diag_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            /* Update timestamp */
            g_diagnostics.uptime_sec = xTaskGetTickCount() / configTICK_RATE_HZ;

            /* Update heap stats */
            g_diagnostics.heap_free_bytes = xPortGetFreeHeapSize();
            g_diagnostics.heap_min_free_bytes = xPortGetMinimumEverFreeHeapSize();

            /* Update stack stats for critical tasks */
            g_diagnostics.udp_rx_stack_hwm = get_stack_hwm("XGW_UDP_RX");
            g_diagnostics.udp_tx_stack_hwm = get_stack_hwm("XGW_UDP_TX");

            /* Get gateway status */
            xgw_gateway_status_t gateway_status;
            xgw_gateway_get_status(&gateway_status);
            g_diagnostics.udp_rx_count = gateway_status.udp_rx_count;
            g_diagnostics.udp_tx_count = gateway_status.udp_tx_count;

            xSemaphoreGive(g_diag_mutex);
        }
    }
}

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * @brief Initialize diagnostics system
 */
int ccu_diag_init(void)
{
    memset(&g_diagnostics, 0, sizeof(g_diagnostics));

    g_diag_mutex = xSemaphoreCreateMutex();
    if (g_diag_mutex == NULL) {
        return -1;
    }

    g_diagnostics.initialized = true;
    return 0;
}

/**
 * @brief Get current diagnostics
 */
int ccu_diag_get(ccu_diagnostics_t* diag)
{
    if (diag == NULL || !g_diagnostics.initialized) {
        return -1;
    }

    if (xSemaphoreTake(g_diag_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(diag, &g_diagnostics, sizeof(ccu_diagnostics_t));
        xSemaphoreGive(g_diag_mutex);
        return 0;
    }

    return -1;
}

/**
 * @brief Log current diagnostics
 */
void ccu_diag_log(void)
{
    update_diagnostics();

    DebugP_log("\r\n");
    DebugP_log("========================================\r\n");
    DebugP_log("  CCU DIAGNOSTICS\r\n");
    DebugP_log("========================================\r\n");
    DebugP_log("  Uptime: %u s\r\n", g_diagnostics.uptime_sec);
    DebugP_log("  Heap: %u free, %u min\r\n",
               g_diagnostics.heap_free_bytes,
               g_diagnostics.heap_min_free_bytes);
    DebugP_log("  Stack HWM: UDP_RX=%u, UDP_TX=%u\r\n",
               g_diagnostics.udp_rx_stack_hwm,
               g_diagnostics.udp_tx_stack_hwm);
    DebugP_log("  Counters: UDP_RX=%lu, UDP_TX=%lu\r\n",
               g_diagnostics.udp_rx_count,
               g_diagnostics.udp_tx_count);
    DebugP_log("========================================\r\n\r\n");
}

/**
 * @brief Check system health
 */
ccu_health_t ccu_diag_check_health(void)
{
    update_diagnostics();

    ccu_health_t health = CCU_HEALTH_OK;

    /* Check heap */
    if (g_diagnostics.heap_free_bytes < CCU_HEAP_CRITICAL_THRESHOLD) {
        health = CCU_HEALTH_HEAP_CRITICAL;
        DebugP_log("[DIAG] Heap critical: %u bytes\r\n", g_diagnostics.heap_free_bytes);
    } else if (g_diagnostics.heap_free_bytes < CCU_HEAP_WARNING_THRESHOLD) {
        health = CCU_HEALTH_HEAP_LOW;
        DebugP_log("[DIAG] Heap low: %u bytes\r\n", g_diagnostics.heap_free_bytes);
    }

    /* Check stack for critical tasks (in words) */
    if (g_diagnostics.udp_rx_stack_hwm < CCU_STACK_CRITICAL_THRESHOLD) {
        health = CCU_HEALTH_STACK_CRITICAL;
        DebugP_log("[DIAG] UDP_RX stack critical: %u words\r\n", g_diagnostics.udp_rx_stack_hwm);
    }
    if (g_diagnostics.udp_tx_stack_hwm < CCU_STACK_CRITICAL_THRESHOLD) {
        health = CCU_HEALTH_STACK_CRITICAL;
        DebugP_log("[DIAG] UDP_TX stack critical: %u words\r\n", g_diagnostics.udp_tx_stack_hwm);
    }

    return health;
}

/**
 * @brief Assert for critical conditions
 */
void ccu_diag_assert_condition(bool condition, const char* msg)
{
    if (!condition) {
        DebugP_log("\r\n");
        DebugP_log("========================================\r\n");
        DebugP_log("  DIAGNOSTIC ASSERT FAILED!\r\n");
        DebugP_log("========================================\r\n");
        DebugP_log("  Condition: %s\r\n", msg ? msg : "NULL");
        ccu_diag_log();

        /* Breakpoint for debugger */
        __asm__ volatile ("BKPT #0");

        while (1) {
            __asm__ volatile ("NOP");
        }
    }
}
