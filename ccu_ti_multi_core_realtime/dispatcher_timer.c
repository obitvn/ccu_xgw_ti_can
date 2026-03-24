/*
 * dispatcher_timer.c
 *
 * Hardware Timer Dispatcher - Periodic Timer Interrupt Handler
 * Multi-core compatible version for Core 1 (NoRTOS)
 *
 * Integration with Core 1 main.c:
 * - dispatcher_isr_callback() is called from main.c's timer_isr()
 * - Provides callback mechanism for user code
 * - Tracks statistics for monitoring
 */

#include "dispatcher_timer.h"
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include <kernel/dpl/DebugP.h>

/* ========================================================================== */
/*                           Local Variables                                  */
/* ========================================================================== */

/* Test counter - visible for verification */
volatile uint32_t g_disp_timer_isr_count = 0;

static volatile DispTimer_State g_timer_state = DISP_TIMER_STATE_STOPPED;
static volatile uint32_t g_tick_count = 0;
static DispTimer_Callback_t g_user_callback = NULL;
static volatile DispTimer_Stats g_stats = {0};
static volatile bool g_callback_in_progress = false;

/* ========================================================================== */
/*                           Local Functions                                  */
/* ========================================================================== */

/**
 * Get current time in nanoseconds using PMU cycle counter
 * This is a simple implementation - can be improved with proper PMU driver
 */
static inline uint32_t get_time_ns(void)
{
    /* Simple implementation using system tick - can be improved */
    /* For now, return 0 as placeholder */
    return 0;
}

/* ========================================================================== */
/*                           Global Functions                                */
/* ========================================================================== */

int32_t disp_timer_init(void)
{
    int32_t status = 0;

    /* Reset state */
    g_timer_state = DISP_TIMER_STATE_STOPPED;
    g_tick_count = 0;
    g_user_callback = NULL;
    g_callback_in_progress = false;

    /* Reset stats */
    g_stats.interrupt_count = 0;
    g_stats.overrun_count = 0;
    g_stats.max_callback_time_ns = 0;
    g_stats.last_callback_time_ns = 0;
    g_stats.error_count = 0;

    DebugP_log("[DispTimer] Initialized (Period: %u us)\r\n", DISP_TIMER_DEFAULT_PERIOD_US);

    return status;
}

int32_t disp_timer_start(void)
{
    int32_t status = 0;

    DebugP_log("[DispTimer] disp_timer_start() called\r\n");

    if (g_timer_state == DISP_TIMER_STATE_RUNNING) {
        DebugP_log("[DispTimer] Already running!\r\n");
        return -1;
    }

    /* Note: Timer is started by main.c using HwiP mechanism
     * This function just marks the state as RUNNING
     * The actual timer start is done in main.c's init_1000hz_timer() */

    g_timer_state = DISP_TIMER_STATE_RUNNING;
    DebugP_log("[DispTimer] Started (state set to RUNNING)\r\n");

    return status;
}

int32_t disp_timer_stop(void)
{
    int32_t status = 0;

    if (g_timer_state == DISP_TIMER_STATE_STOPPED) {
        return 0;
    }

    /* Note: Timer is stopped by main.c
     * This function just marks the state as STOPPED */

    g_timer_state = DISP_TIMER_STATE_STOPPED;
    DebugP_log("[DispTimer] Stopped (state set to STOPPED)\r\n");

    return status;
}

int32_t disp_timer_register_callback(DispTimer_Callback_t callback)
{
    if (callback == NULL) {
        DebugP_log("[DispTimer] NULL callback!\r\n");
        return -1;
    }

    g_user_callback = callback;
    DebugP_log("[DispTimer] Callback registered\r\n");

    return 0;
}

DispTimer_State disp_timer_get_state(void)
{
    return g_timer_state;
}

int32_t disp_timer_get_stats(DispTimer_Stats *stats)
{
    if (stats == NULL) {
        return -1;
    }

    *stats = (DispTimer_Stats)g_stats;

    return 0;
}

void disp_timer_reset_stats(void)
{
    g_stats.interrupt_count = 0;
    g_stats.overrun_count = 0;
    g_stats.max_callback_time_ns = 0;
    g_stats.last_callback_time_ns = 0;
    g_stats.error_count = 0;
}

uint32_t disp_timer_get_tick_count(void)
{
    return g_tick_count;
}

/**
 * Dispatcher Timer ISR Callback
 *
 * This function is called from main.c's timer_isr() at 1000Hz
 * It performs the following:
 * 1. Updates statistics
 * 2. Calls user callback if registered
 * 3. Tracks callback execution time
 *
 * NOTE: This function runs in ISR context - keep it minimal!
 */
void disp_timer_isr_callback(void *args)
{
    (void)args;  /* Unused */

    /* Increment counters */
    g_disp_timer_isr_count++;
    g_tick_count++;
    g_stats.interrupt_count++;

    /* Check if previous callback is still running (overrun detection) */
    if (g_callback_in_progress) {
        g_stats.overrun_count++;
        /* Don't call callback again if previous one hasn't finished */
        return;
    }

    /* Call user callback if registered */
    if (g_user_callback != NULL) {
        g_callback_in_progress = true;

        /* TODO: Measure callback execution time */
        /* uint32_t start_time = get_time_ns(); */

        g_user_callback();

        /* TODO: Calculate callback execution time */
        /* uint32_t end_time = get_time_ns(); */
        /* uint32_t exec_time = end_time - start_time; */

        g_callback_in_progress = false;
    }
}
