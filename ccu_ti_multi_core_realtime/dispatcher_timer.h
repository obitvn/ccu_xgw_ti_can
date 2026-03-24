/*
 * dispatcher_timer.h
 *
 * Hardware Timer Dispatcher - Periodic Timer Interrupt Handler
 *
 */

#ifndef DISPATCHER_TIMER_H_
#define DISPATCHER_TIMER_H_

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================== */
/*                           Macros & Typedefs                                */
/* ========================================================================== */

/* Default timer configuration */
#define DISP_TIMER_DEFAULT_PERIOD_US    500U    /* Default: 500us (nsecPerTick0 = 500000 in syscfg) */

/* Timer state */
typedef enum {
    DISP_TIMER_STATE_STOPPED = 0,
    DISP_TIMER_STATE_RUNNING,
} DispTimer_State;

/* Timer statistics */
typedef struct {
    uint32_t interrupt_count;         /* Total interrupt count */
    uint32_t overrun_count;           /* Overrun count (if previous callback not done) */
    uint32_t max_callback_time_ns;    /* Max callback execution time in ns */
    uint32_t last_callback_time_ns;   /* Last callback execution time in ns */
    uint32_t error_count;             /* Error count */
} DispTimer_Stats;

/* Timer callback function type */
typedef void (*DispTimer_Callback_t)(void);

/* Test counter - incremented in ISR for verification */
extern volatile uint32_t g_disp_timer_isr_count;

/* ========================================================================== */
/*                           Function Declarations                            */
/* ========================================================================== */

/**
 * Initialize the periodic timer dispatcher
 *
 * @return  0 on success, -1 on failure
 */
int32_t disp_timer_init(void);

/**
 * Start the periodic timer dispatcher
 *
 * @return  0 on success, -1 on failure
 */
int32_t disp_timer_start(void);

/**
 * Stop the periodic timer dispatcher
 *
 * @return  0 on success, -1 on failure
 */
int32_t disp_timer_stop(void);

/**
 * Register a callback function for periodic timer interrupt
 *
 * @param   callback    Pointer to callback function
 * @return  0 on success, -1 on failure
 */
int32_t disp_timer_register_callback(DispTimer_Callback_t callback);

/**
 * Get timer state
 *
 * @return  Current timer state
 */
DispTimer_State disp_timer_get_state(void);

/**
 * Get timer statistics
 *
 * @param   stats   Pointer to stats structure to fill
 * @return  0 on success, -1 on failure
 */
int32_t disp_timer_get_stats(DispTimer_Stats *stats);

/**
 * Reset timer statistics
 */
void disp_timer_reset_stats(void);

/**
 * Get current tick count
 *
 * @return  Current tick count
 */
uint32_t disp_timer_get_tick_count(void);

/**
 * RTI Timer interrupt callback (called from ISR)
 * This function is registered in syscfg and called by the RTI driver
 * MUST have (void *args) parameter to match generated code!
 */
void disp_timer_isr_callback(void *args);

#endif /* DISPATCHER_TIMER_H_ */
