/**
 * @file ccu_log.c
 * @brief Centralized logging wrapper implementation
 *
 * @author Chu Tien Thinh
 * @date 2025
 */

#include "ccu_log.h"
#include "syslog.h"
#include <kernel/dpl/DebugP.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/*==============================================================================
 * INTERNAL STATE
 *============================================================================*/

/**
 * @brief Logging state
 */
static struct {
    log_output_mode_t output_mode;
    log_level_t log_level;
    bool initialized;
} g_ccu_log = {
    .output_mode = LOG_OUTPUT_MODE,
    .log_level = LOG_LEVEL,
    .initialized = false
};

/*==============================================================================
 * INTERNAL FUNCTIONS
 *============================================================================*/

/**
 * @brief Format log message into buffer
 */
static int format_log_message(char* buf, size_t buf_len,
                               const char* tag, const char* fmt, va_list args)
{
    int offset = 0;

    /* Add tag if provided */
    if (tag != NULL && tag[0] != '\0') {
        offset += snprintf(buf + offset, buf_len - offset, "[%s] ", tag);
    }

    /* Add formatted message */
    offset += vsnprintf(buf + offset, buf_len - offset, fmt, args);

    return offset;
}

/**
 * @brief Send log to UART
 */
static void log_to_uart(const char* msg)
{
    DebugP_log("%s\r\n", msg);
}

/**
 * @brief Send log to Syslog
 */
static void log_to_syslog(int severity, const char* tag, const char* msg)
{
    /* Map our log levels to syslog severity levels */
    syslog_severity_t syslog_severity;

    switch (severity) {
        case LOG_LEVEL_ERROR:
            syslog_severity = SYSLOG_SEV_ERROR;
            break;
        case LOG_LEVEL_WARN:
            syslog_severity = SYSLOG_SEV_WARNING;
            break;
        case LOG_LEVEL_INFO:
            syslog_severity = SYSLOG_SEV_INFO;
            break;
        case LOG_LEVEL_DEBUG:
        default:
            syslog_severity = SYSLOG_SEV_DEBUG;
            break;
    }

    /* Map to facility - use LOCAL0 for custom application */
    Syslog_Send(SYSLOG_FAC_LOCAL0, syslog_severity, tag, "%s", msg);
}

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

int ccu_log_init(void)
{
    if (g_ccu_log.initialized) {
        return 0;  /* Already initialized */
    }

    /* Initialize syslog (if configured in mode) */
    if (g_ccu_log.output_mode == LOG_OUTPUT_SYSLOG ||
        g_ccu_log.output_mode == LOG_OUTPUT_BOTH) {
        if (Syslog_Init(NULL) != 0) {
            /* Syslog init failed, but don't fail - continue with UART only */
            DebugP_log("[CCU_LOG] Syslog init failed, using UART only\r\n");
            g_ccu_log.output_mode = LOG_OUTPUT_UART;
        }
    }

    g_ccu_log.initialized = true;

    DebugP_log("[CCU_LOG] Initialized - Mode: %d (0=NONE,1=UART,2=SYSLOG,3=BOTH), Level: %d\r\n",
             g_ccu_log.output_mode, g_ccu_log.log_level);

    return 0;
}

void ccu_log_deinit(void)
{
    if (!g_ccu_log.initialized) {
        return;
    }

    /* Deinitialize syslog (if it was initialized) */
    if (g_ccu_log.output_mode == LOG_OUTPUT_SYSLOG ||
        g_ccu_log.output_mode == LOG_OUTPUT_BOTH) {
        Syslog_Deinit();
    }

    g_ccu_log.initialized = false;
}

void ccu_log_set_mode(log_output_mode_t mode)
{
    g_ccu_log.output_mode = mode;
}

log_output_mode_t ccu_log_get_mode(void)
{
    return g_ccu_log.output_mode;
}

void ccu_log_set_level(log_level_t level)
{
    if (level >= LOG_LEVEL_ERROR && level <= LOG_LEVEL_VERBOSE) {
        g_ccu_log.log_level = level;
    }
}

log_level_t ccu_log_get_level(void)
{
    return g_ccu_log.log_level;
}

/*==============================================================================
 * LOG FUNCTIONS
 *============================================================================*/

void ccu_log_debug(const char* tag, const char* fmt, ...)
{
    if (g_ccu_log.log_level < LOG_LEVEL_DEBUG) {
        return;
    }

    if (g_ccu_log.output_mode == LOG_OUTPUT_NONE) {
        return;
    }

    char msg_buf[256];
    va_list args;
    va_start(args, fmt);

    format_log_message(msg_buf, sizeof(msg_buf), tag, fmt, args);
    va_end(args);

    /* Route to appropriate output(s) based on runtime mode */
    if (g_ccu_log.output_mode == LOG_OUTPUT_UART ||
        g_ccu_log.output_mode == LOG_OUTPUT_BOTH) {
        log_to_uart(msg_buf);
    }

    if (g_ccu_log.output_mode == LOG_OUTPUT_SYSLOG ||
        g_ccu_log.output_mode == LOG_OUTPUT_BOTH) {
        log_to_syslog(LOG_LEVEL_DEBUG, tag, msg_buf);
    }
}

void ccu_log_info(const char* tag, const char* fmt, ...)
{
    if (g_ccu_log.log_level < LOG_LEVEL_INFO) {
        return;
    }

    if (g_ccu_log.output_mode == LOG_OUTPUT_NONE) {
        return;
    }

    char msg_buf[256];
    va_list args;
    va_start(args, fmt);

    format_log_message(msg_buf, sizeof(msg_buf), tag, fmt, args);
    va_end(args);

    /* Route to appropriate output(s) based on runtime mode */
    if (g_ccu_log.output_mode == LOG_OUTPUT_UART ||
        g_ccu_log.output_mode == LOG_OUTPUT_BOTH) {
        log_to_uart(msg_buf);
    }

    if (g_ccu_log.output_mode == LOG_OUTPUT_SYSLOG ||
        g_ccu_log.output_mode == LOG_OUTPUT_BOTH) {
        log_to_syslog(LOG_LEVEL_INFO, tag, msg_buf);
    }
}

void ccu_log_warn(const char* tag, const char* fmt, ...)
{
    if (g_ccu_log.log_level < LOG_LEVEL_WARN) {
        return;
    }

    if (g_ccu_log.output_mode == LOG_OUTPUT_NONE) {
        return;
    }

    char msg_buf[256];
    va_list args;
    va_start(args, fmt);

    format_log_message(msg_buf, sizeof(msg_buf), tag, fmt, args);
    va_end(args);

    /* Route to appropriate output(s) based on runtime mode */
    if (g_ccu_log.output_mode == LOG_OUTPUT_UART ||
        g_ccu_log.output_mode == LOG_OUTPUT_BOTH) {
        log_to_uart(msg_buf);
    }

    if (g_ccu_log.output_mode == LOG_OUTPUT_SYSLOG ||
        g_ccu_log.output_mode == LOG_OUTPUT_BOTH) {
        log_to_syslog(LOG_LEVEL_WARN, tag, msg_buf);
    }
}

void ccu_log_error(const char* tag, const char* fmt, ...)
{
    if (g_ccu_log.log_level < LOG_LEVEL_ERROR) {
        return;
    }

    if (g_ccu_log.output_mode == LOG_OUTPUT_NONE) {
        return;
    }

    char msg_buf[256];
    va_list args;
    va_start(args, fmt);

    format_log_message(msg_buf, sizeof(msg_buf), tag, fmt, args);
    va_end(args);

    /* Route to appropriate output(s) based on runtime mode */
    if (g_ccu_log.output_mode == LOG_OUTPUT_UART ||
        g_ccu_log.output_mode == LOG_OUTPUT_BOTH) {
        log_to_uart(msg_buf);
    }

    if (g_ccu_log.output_mode == LOG_OUTPUT_SYSLOG ||
        g_ccu_log.output_mode == LOG_OUTPUT_BOTH) {
        log_to_syslog(LOG_LEVEL_ERROR, tag, msg_buf);
    }
}

void ccu_log_crit(const char* tag, const char* fmt, ...)
{
    /* Critical messages always logged regardless of level */
    if (g_ccu_log.output_mode == LOG_OUTPUT_NONE) {
        return;
    }

    char msg_buf[256];
    va_list args;
    va_start(args, fmt);

    format_log_message(msg_buf, sizeof(msg_buf), tag, fmt, args);
    va_end(args);

    /* Route to appropriate output(s) based on runtime mode */
    if (g_ccu_log.output_mode == LOG_OUTPUT_UART ||
        g_ccu_log.output_mode == LOG_OUTPUT_BOTH) {
        log_to_uart(msg_buf);
    }

    if (g_ccu_log.output_mode == LOG_OUTPUT_SYSLOG ||
        g_ccu_log.output_mode == LOG_OUTPUT_BOTH) {
        log_to_syslog(LOG_LEVEL_ERROR, tag, msg_buf);
    }
}

void ccu_log_dual(const char* tag, const char* fmt, ...)
{
    char msg_buf[256];
    va_list args;
    va_start(args, fmt);

    format_log_message(msg_buf, sizeof(msg_buf), tag, fmt, args);
    va_end(args);

    /* Always output to BOTH UART and Syslog regardless of mode */
    log_to_uart(msg_buf);
    log_to_syslog(LOG_LEVEL_INFO, tag, msg_buf);
}
