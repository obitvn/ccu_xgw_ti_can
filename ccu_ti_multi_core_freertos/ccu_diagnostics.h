/**
 * @file ccu_diagnostics.h
 * @brief Runtime diagnostics and monitoring for ccu_ti
 */

#ifndef CCU_DIAGNOSTICS_H_
#define CCU_DIAGNOSTICS_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

/* Health thresholds */
#define CCU_HEAP_CRITICAL_THRESHOLD    (10240U)    /* 10KB */
#define CCU_HEAP_WARNING_THRESHOLD     (20480U)    /* 20KB */
#define CCU_STACK_CRITICAL_THRESHOLD   (256U)      /* 256 words = 1KB */

/* Health status codes */
typedef enum {
    CCU_HEALTH_OK = 0,
    CCU_HEALTH_HEAP_LOW,
    CCU_HEALTH_HEAP_CRITICAL,
    CCU_HEALTH_STACK_CRITICAL,
    CCU_HEALTH_ERROR
} ccu_health_t;

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

/**
 * @brief Diagnostics data structure
 */
typedef struct {
    /* Status flags */
    bool initialized;

    /* Uptime */
    uint32_t uptime_sec;

    /* Heap statistics */
    uint32_t heap_free_bytes;
    uint32_t heap_min_free_bytes;

    /* Stack high water marks (in words) */
    uint32_t udp_rx_stack_hwm;
    uint32_t udp_tx_stack_hwm;

    /* Traffic counters (Core0 only - UDP) */
    uint32_t udp_rx_count;
    uint32_t udp_tx_count;

    /* Error counters */
    uint32_t parse_errors;
    uint32_t crc_errors;
    uint32_t mutex_failures;

} ccu_diagnostics_t;

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * @brief Initialize diagnostics system
 * @return 0 on success, -1 on error
 */
int ccu_diag_init(void);

/**
 * @brief Get current diagnostics
 * @param diag Output diagnostics structure
 * @return 0 on success, -1 on error
 */
int ccu_diag_get(ccu_diagnostics_t* diag);

/**
 * @brief Log current diagnostics to UART
 */
void ccu_diag_log(void);

/**
 * @brief Check system health
 * @return Health status code
 */
ccu_health_t ccu_diag_check_health(void);

/**
 * @brief Assert for critical conditions with diagnostics
 * @param condition Condition to check
 * @param msg Description message
 */
void ccu_diag_assert_condition(bool condition, const char* msg);

/* Convenience macros for diagnostics */
#define CCU_DIAG_ASSERT(cond) ccu_diag_assert_condition((cond), #cond)
#define CCU_DIAG_ASSERT_MSG(cond, msg) ccu_diag_assert_condition((cond), msg)

#define CCU_DIAG_LOG() ccu_diag_log()
#define CCU_DIAG_CHECK() ccu_diag_check_health()

#ifdef __cplusplus
}
#endif

#endif /* CCU_DIAGNOSTICS_H_ */
