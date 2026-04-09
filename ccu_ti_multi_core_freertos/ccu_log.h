/**
 * @file ccu_log.h
 * @brief Centralized logging wrapper with output mode switching
 *
 * Supports switching between UART, Syslog, Both, or None
 *
 * @author Chu Tien Thinh
 * @date 2025
 */

#ifndef CCU_LOG_H_
#define CCU_LOG_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * LOG OUTPUT MODE CONFIGURATION
 *============================================================================*/

/**
 * @brief Log output mode selection
 *
 * Configure this define to select where logs are sent:
 * - LOG_OUTPUT_NONE:  Disable all logging (no output)
 * - LOG_OUTPUT_UART:  Output to UART0 only (DebugP_log)
 * - LOG_OUTPUT_SYSLOG: Output to Syslog only (UDP network)
 * - LOG_OUTPUT_BOTH:  Output to both UART0 and Syslog
 *
 * Default: LOG_OUTPUT_BOTH
 */
#ifndef LOG_OUTPUT_MODE
#define LOG_OUTPUT_MODE  LOG_OUTPUT_BOTH
#endif

/**
 * @brief Log output mode enum
 */
typedef enum {
    LOG_OUTPUT_NONE = 0,       /* Disable all logging */
    LOG_OUTPUT_UART = 1,       /* UART0 only */
    LOG_OUTPUT_SYSLOG = 2,     /* Syslog (UDP) only */
    LOG_OUTPUT_BOTH = 3        /* Both UART0 and Syslog */
} log_output_mode_t;

/*==============================================================================
 * LOG LEVEL CONFIGURATION
 *============================================================================*/

/**
 * @brief Log levels for filtering
 */
typedef enum {
    LOG_LEVEL_ERROR = 0,       /* Error messages only */
    LOG_LEVEL_WARN = 1,        /* Warnings and errors */
    LOG_LEVEL_INFO = 2,        /* Info, warnings, errors */
    LOG_LEVEL_DEBUG = 3,       /* All messages including debug */
    LOG_LEVEL_VERBOSE = 4      /* Very verbose logging */
} log_level_t;

/**
 * @brief Current log level (compile-time configurable)
 *
 * Only messages at or below this level will be output.
 * This helps reduce log noise in production.
 */
#ifndef LOG_LEVEL
#define LOG_LEVEL  LOG_LEVEL_INFO
#endif

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * @brief Initialize logging system
 *
 * Must be called after lwIP stack is initialized (for syslog support)
 *
 * @return 0 on success, -1 on error
 */
int ccu_log_init(void);

/**
 * @brief Deinitialize logging system
 */
void ccu_log_deinit(void);

/**
 * @brief Set log output mode at runtime
 *
 * @param mode Output mode (LOG_OUTPUT_UART, LOG_OUTPUT_SYSLOG, LOG_OUTPUT_BOTH, LOG_OUTPUT_NONE)
 */
void ccu_log_set_mode(log_output_mode_t mode);

/**
 * @brief Get current log output mode
 *
 * @return Current output mode
 */
log_output_mode_t ccu_log_get_mode(void);

/**
 * @brief Set minimum log level at runtime
 *
 * @param level Minimum log level
 */
void ccu_log_set_level(log_level_t level);

/**
 * @brief Get current log level
 *
 * @return Current log level
 */
log_level_t ccu_log_get_level(void);

/*==============================================================================
 * LOG FUNCTIONS
 *============================================================================*/

/**
 * @brief Debug log message
 *
 * @param tag Module tag (can be NULL)
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void ccu_log_debug(const char* tag, const char* fmt, ...);

/**
 * @brief Info log message
 *
 * @param tag Module tag (can be NULL)
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void ccu_log_info(const char* tag, const char* fmt, ...);

/**
 * @brief Warning log message
 *
 * @param tag Module tag (can be NULL)
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void ccu_log_warn(const char* tag, const char* fmt, ...);

/**
 * @brief Error log message
 *
 * @param tag Module tag (can be NULL)
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void ccu_log_error(const char* tag, const char* fmt, ...);

/**
 * @brief Critical/Error log message (alias for error)
 *
 * @param tag Module tag (can be NULL)
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void ccu_log_crit(const char* tag, const char* fmt, ...);

/**
 * @brief Log to both UART (DebugP_log) and Syslog
 *
 * Convenience function that automatically outputs to both UART and Syslog
 * regardless of the current output mode. Useful for existing code migration.
 *
 * @param tag Module tag (can be NULL)
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void ccu_log_dual(const char* tag, const char* fmt, ...);

/*==============================================================================
 * CONVENIENCE MACROS
 *============================================================================*/

/* Macros with automatic file and line number */
#define LOG_DEBUG(tag, fmt, ...)    ccu_log_debug(tag, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)     ccu_log_info(tag, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)     ccu_log_warn(tag, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...)    ccu_log_error(tag, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_CRIT(tag, fmt, ...)     ccu_log_crit(tag, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

/* Legacy macros for compatibility with existing code */
#define xgw_debug_log(fmt, ...)     ccu_log_debug("XGW", fmt, ##__VA_ARGS__)
#define xgw_error_log(fmt, ...)     ccu_log_error("XGW", fmt, ##__VA_ARGS__)

/*==============================================================================
 * CONDITIONAL LOGGING MACROS
 *============================================================================*/

/**
 * @brief Log only if specified level is enabled
 *
 * Usage: LOG_IF_LEVEL(LOG_LEVEL_DEBUG, "Debug message: %d", value);
 */
#define LOG_IF_LEVEL(level, fmt, ...) \
    do { \
        if (LOG_LEVEL >= (level)) { \
            LOG_DEBUG(NULL, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

/**
 * @brief Log debug messages only in debug builds
 */
#ifdef DEBUG
    #define LOG_DEBUG_BUILD(fmt, ...)  LOG_DEBUG("DBG", fmt, ##__VA_ARGS__)
#else
    #define LOG_DEBUG_BUILD(fmt, ...)  do {} while(0)
#endif

/*==============================================================================
 * [FIX B097] ASYNC LOGGING QUEUE API
 *============================================================================*/

/* Async log configuration */
#define CCU_LOG_QUEUE_SIZE       (16 * 1024)  /* 16KB ring buffer */
#define CCU_LOG_MAX_MSG_LEN      512           /* Max single log message */

/* Logger task configuration */
#define LOGGER_TASK_SIZE         (4096U/sizeof(StackType_t))  /* 4KB stack */
#define LOGGER_TASK_PRI          2   /* Lowest priority - background logging */

/**
 * @brief Log queue statistics
 */
typedef struct {
    uint32_t pushed;          /* Total messages pushed */
    uint32_t popped;          /* Total messages popped */
    uint32_t dropped;         /* Messages dropped (queue full) */
    uint32_t overruns;        /* Bytes overwritten (partial drops) */
    uint32_t current_bytes;   /* Current bytes in queue */
    uint32_t peak_bytes;      /* Peak bytes in queue */
} ccu_log_queue_stats_t;

/**
 * @brief Initialize async logging queue
 *
 * Must be called before using async log functions.
 * Creates ring buffer and synchronization primitives.
 *
 * @return 0 on success, -1 on error
 */
int ccu_log_queue_init(void);

/**
 * @brief Check if async log queue is initialized
 *
 * @return true if initialized, false otherwise
 */
bool ccu_log_queue_is_initialized(void);

/**
 * @brief Push log message to queue (non-blocking)
 *
 * Formats both syslog and UART messages and stores in queue.
 * If queue is full, oldest message is dropped (no blocking).
 *
 * @param level    Log level
 * @param tag      Module tag (can be NULL)
 * @param fmt      Printf-style format string
 * @param ...      Format arguments
 * @return         0 on success (pushed or dropped), -1 on error
 */
int ccu_log_push(log_level_t level, const char* tag, const char* fmt, ...);

/**
 * @brief Get log queue statistics
 *
 * @param stats  Pointer to stats structure to fill
 */
void ccu_log_queue_get_stats(ccu_log_queue_stats_t* stats);

/**
 * @brief Reset log queue statistics
 */
void ccu_log_queue_reset_stats(void);

/**
 * @brief Get current queue usage (bytes)
 *
 * @return Current bytes used in queue
 */
uint32_t ccu_log_queue_get_usage(void);

#ifdef __cplusplus
}
#endif

#endif /* CCU_LOG_H_ */
