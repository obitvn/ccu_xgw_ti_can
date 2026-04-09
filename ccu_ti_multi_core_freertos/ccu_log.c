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
 * [FIX B097] ASYNC LOGGING QUEUE - STATE AND TYPES
 *============================================================================*/

#include <FreeRTOS.h>
#include <semphr.h>

/* Async log queue state */
static struct {
    /* Ring buffer */
    uint8_t buffer[CCU_LOG_QUEUE_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;

    /* State */
    bool queue_initialized;
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t sem;

    /* Statistics */
    ccu_log_queue_stats_t stats;
} g_log_queue = {0};

/**
 * @brief Log queue entry header
 */
typedef struct {
    uint16_t syslog_len;
    uint16_t uart_len;
    uint8_t level;
    uint8_t priority;
} log_entry_header_t;

/* Forward declaration for logger task */
static void logger_task(void *args);

/* Logger task handle and stack (shared with main.c) */
extern TaskHandle_t gLoggerTask;
extern StackType_t gLoggerTaskStack[LOGGER_TASK_SIZE];
extern StaticTask_t gLoggerTaskObj;

/* Forward declaration for internal async push function */
static int ccu_log_push_v(log_level_t level, const char* tag, const char* fmt, va_list args);

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

    /* [FIX B097] Use async queue if initialized, otherwise direct blocking I/O */
    va_list args;
    va_start(args, fmt);

    if (g_log_queue.queue_initialized) {
        ccu_log_push_v(LOG_LEVEL_DEBUG, tag, fmt, args);
    } else {
        /* Fallback to direct blocking I/O */
        char msg_buf[256];
        format_log_message(msg_buf, sizeof(msg_buf), tag, fmt, args);

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

    va_end(args);
}

void ccu_log_info(const char* tag, const char* fmt, ...)
{
    if (g_ccu_log.log_level < LOG_LEVEL_INFO) {
        return;
    }

    if (g_ccu_log.output_mode == LOG_OUTPUT_NONE) {
        return;
    }

    /* [FIX B097] Use async queue if initialized, otherwise direct blocking I/O */
    va_list args;
    va_start(args, fmt);

    if (g_log_queue.queue_initialized) {
        ccu_log_push_v(LOG_LEVEL_INFO, tag, fmt, args);
    } else {
        /* Fallback to direct blocking I/O */
        char msg_buf[256];
        format_log_message(msg_buf, sizeof(msg_buf), tag, fmt, args);

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

    va_end(args);
}

void ccu_log_warn(const char* tag, const char* fmt, ...)
{
    if (g_ccu_log.log_level < LOG_LEVEL_WARN) {
        return;
    }

    if (g_ccu_log.output_mode == LOG_OUTPUT_NONE) {
        return;
    }

    /* [FIX B097] Use async queue if initialized, otherwise direct blocking I/O */
    va_list args;
    va_start(args, fmt);

    if (g_log_queue.queue_initialized) {
        ccu_log_push_v(LOG_LEVEL_WARN, tag, fmt, args);
    } else {
        /* Fallback to direct blocking I/O */
        char msg_buf[256];
        format_log_message(msg_buf, sizeof(msg_buf), tag, fmt, args);

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

    va_end(args);
}

void ccu_log_error(const char* tag, const char* fmt, ...)
{
    if (g_ccu_log.log_level < LOG_LEVEL_ERROR) {
        return;
    }

    if (g_ccu_log.output_mode == LOG_OUTPUT_NONE) {
        return;
    }

    /* [FIX B097] Use async queue if initialized, otherwise direct blocking I/O */
    va_list args;
    va_start(args, fmt);

    if (g_log_queue.queue_initialized) {
        ccu_log_push_v(LOG_LEVEL_ERROR, tag, fmt, args);
    } else {
        /* Fallback to direct blocking I/O */
        char msg_buf[256];
        format_log_message(msg_buf, sizeof(msg_buf), tag, fmt, args);

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

    va_end(args);
}

void ccu_log_crit(const char* tag, const char* fmt, ...)
{
    /* Critical messages always logged regardless of level */
    if (g_ccu_log.output_mode == LOG_OUTPUT_NONE) {
        return;
    }

    /* [FIX B097] Use async queue if initialized, otherwise direct blocking I/O */
    va_list args;
    va_start(args, fmt);

    if (g_log_queue.queue_initialized) {
        ccu_log_push_v(LOG_LEVEL_ERROR, tag, fmt, args);
    } else {
        /* Fallback to direct blocking I/O */
        char msg_buf[256];
        format_log_message(msg_buf, sizeof(msg_buf), tag, fmt, args);

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

    va_end(args);
}

void ccu_log_dual(const char* tag, const char* fmt, ...)
{
    /* [FIX B097] Use async queue if initialized, otherwise direct blocking I/O */
    va_list args;
    va_start(args, fmt);

    if (g_log_queue.queue_initialized) {
        ccu_log_push_v(LOG_LEVEL_INFO, tag, fmt, args);
    } else {
        /* Fallback to direct blocking I/O */
        char msg_buf[256];
        format_log_message(msg_buf, sizeof(msg_buf), tag, fmt, args);

        /* Always output to BOTH UART and Syslog regardless of mode */
        log_to_uart(msg_buf);
        log_to_syslog(LOG_LEVEL_INFO, tag, msg_buf);
    }

    va_end(args);
}

/**
 * @brief Calculate available space in ring buffer
 */
static inline uint32_t queue_get_available(void)
{
    uint32_t head = g_log_queue.head;
    uint32_t tail = g_log_queue.tail;

    if (head >= tail) {
        return CCU_LOG_QUEUE_SIZE - (head - tail) - 1;
    } else {
        return tail - head - 1;
    }
}

/**
 * @brief Calculate used space in ring buffer
 */
static inline uint32_t queue_get_used(void)
{
    return CCU_LOG_QUEUE_SIZE - queue_get_available() - 1;
}

/**
 * @brief Write data to ring buffer
 */
static bool queue_write(const uint8_t* data, uint32_t len)
{
    uint32_t head = g_log_queue.head;
    uint32_t available = queue_get_available();

    if (len > available) {
        return false;
    }

    /* Write until end or until all data written */
    uint32_t first_part = CCU_LOG_QUEUE_SIZE - head;
    if (first_part > len) {
        first_part = len;
    }

    memcpy(&g_log_queue.buffer[head], data, first_part);

    if (len > first_part) {
        memcpy(&g_log_queue.buffer[0], data + first_part, len - first_part);
    }

    g_log_queue.head = (head + len) % CCU_LOG_QUEUE_SIZE;
    return true;
}

/**
 * @brief Read data from ring buffer
 */
static bool queue_read(uint8_t* data, uint32_t len)
{
    uint32_t tail = g_log_queue.tail;
    uint32_t used = queue_get_used();

    if (len > used) {
        return false;
    }

    uint32_t first_part = CCU_LOG_QUEUE_SIZE - tail;
    if (first_part > len) {
        first_part = len;
    }

    memcpy(data, &g_log_queue.buffer[tail], first_part);

    if (len > first_part) {
        memcpy(data + first_part, &g_log_queue.buffer[0], len - first_part);
    }

    g_log_queue.tail = (tail + len) % CCU_LOG_QUEUE_SIZE;
    return true;
}

/**
 * @brief Format syslog message
 */
static uint32_t format_syslog_msg(char* buf, size_t buf_len, log_level_t level,
                                  const char* tag, const char* msg)
{
    syslog_severity_t severity;
    switch (level) {
        case LOG_LEVEL_ERROR: severity = SYSLOG_SEV_ERROR; break;
        case LOG_LEVEL_WARN:  severity = SYSLOG_SEV_WARNING; break;
        case LOG_LEVEL_INFO:  severity = SYSLOG_SEV_INFO; break;
        case LOG_LEVEL_DEBUG:
        default: severity = SYSLOG_SEV_DEBUG; break;
    }

    int len = snprintf(buf, buf_len, "[%s] %s", tag ? tag : "APP", msg);
    if (len < 0 || len >= (int)buf_len) {
        return 0;
    }
    return len;
}

/**
 * @brief Format UART message
 */
static uint32_t format_uart_msg(char* buf, size_t buf_len, log_level_t level,
                                const char* tag, const char* msg)
{
    const char* level_str;
    switch (level) {
        case LOG_LEVEL_ERROR: level_str = "ERROR"; break;
        case LOG_LEVEL_WARN:  level_str = "WARN "; break;
        case LOG_LEVEL_INFO:  level_str = "INFO "; break;
        case LOG_LEVEL_DEBUG: level_str = "DEBUG"; break;
        default:              level_str = "LOG  "; break;
    }

    int len = snprintf(buf, buf_len, "[%s] [%s] %s", level_str, tag ? tag : "APP", msg);
    if (len < 0 || len >= (int)buf_len) {
        return 0;
    }
    return len;
}

/*==============================================================================
 * ASYNC LOGGING QUEUE PUBLIC API
 *============================================================================*/

int ccu_log_queue_init(void)
{
    if (g_log_queue.queue_initialized) {
        return 0;
    }

    memset(&g_log_queue, 0, sizeof(g_log_queue));

    /* Create mutex */
    g_log_queue.mutex = xSemaphoreCreateMutex();
    if (g_log_queue.mutex == NULL) {
        return -1;
    }

    /* Create semaphore */
    g_log_queue.sem = xSemaphoreCreateBinary();
    if (g_log_queue.sem == NULL) {
        vSemaphoreDelete(g_log_queue.mutex);
        return -1;
    }

    g_log_queue.queue_initialized = true;
    return 0;
}

bool ccu_log_queue_is_initialized(void)
{
    return g_log_queue.queue_initialized;
}

/* [FIX B097] Internal helper: push log with va_list */
static int ccu_log_push_v(log_level_t level, const char* tag, const char* fmt, va_list args)
{
    if (!g_log_queue.queue_initialized) {
        return -1;
    }

    /* Format message */
    char msg_buf[CCU_LOG_MAX_MSG_LEN];
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);

    /* Format syslog and UART versions */
    char syslog_buf[CCU_LOG_MAX_MSG_LEN];
    char uart_buf[CCU_LOG_MAX_MSG_LEN];
    uint32_t syslog_len = format_syslog_msg(syslog_buf, sizeof(syslog_buf), level, tag, msg_buf);
    uint32_t uart_len = format_uart_msg(uart_buf, sizeof(uart_buf), level, tag, msg_buf);

    uint32_t total_size = sizeof(log_entry_header_t) + syslog_len + uart_len;

    /* Try to acquire mutex (non-blocking) */
    if (xSemaphoreTake(g_log_queue.mutex, 0) != pdTRUE) {
        g_log_queue.stats.dropped++;
        return 0;
    }

    /* Check space and make room if needed */
    uint32_t available = queue_get_available();
    if (total_size > available) {
        uint32_t needed = total_size - available;
        g_log_queue.tail = (g_log_queue.tail + needed) % CCU_LOG_QUEUE_SIZE;
        g_log_queue.stats.overruns += needed;
        g_log_queue.stats.dropped++;
    }

    /* Write header */
    log_entry_header_t header = {
        .syslog_len = (uint16_t)syslog_len,
        .uart_len = (uint16_t)uart_len,
        .level = (uint8_t)level,
        .priority = 0
    };

    if (!queue_write((uint8_t*)&header, sizeof(header))) {
        xSemaphoreGive(g_log_queue.mutex);
        g_log_queue.stats.dropped++;
        return 0;
    }

    /* Write syslog data */
    if (syslog_len > 0) {
        if (!queue_write((uint8_t*)syslog_buf, syslog_len)) {
            xSemaphoreGive(g_log_queue.mutex);
            g_log_queue.stats.dropped++;
            return 0;
        }
    }

    /* Write UART data */
    if (uart_len > 0) {
        if (!queue_write((uint8_t*)uart_buf, uart_len)) {
            xSemaphoreGive(g_log_queue.mutex);
            g_log_queue.stats.dropped++;
            return 0;
        }
    }

    g_log_queue.stats.pushed++;
    g_log_queue.stats.current_bytes = queue_get_used();
    if (g_log_queue.stats.current_bytes > g_log_queue.stats.peak_bytes) {
        g_log_queue.stats.peak_bytes = g_log_queue.stats.current_bytes;
    }

    xSemaphoreGive(g_log_queue.mutex);

    /* Signal logger task */
    xSemaphoreGive(g_log_queue.sem);

    return 0;
}

int ccu_log_push(log_level_t level, const char* tag, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = ccu_log_push_v(level, tag, fmt, args);
    va_end(args);
    return ret;
}

static int logger_pop_and_flush(uint32_t timeout_ms)
{
    if (!g_log_queue.queue_initialized) {
        return -1;
    }

    if (xSemaphoreTake(g_log_queue.sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return -2;
    }

    if (xSemaphoreTake(g_log_queue.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }

    uint32_t used = queue_get_used();
    if (used < sizeof(log_entry_header_t)) {
        xSemaphoreGive(g_log_queue.mutex);
        return -1;
    }

    /* Read header */
    log_entry_header_t header;
    if (!queue_read((uint8_t*)&header, sizeof(header))) {
        xSemaphoreGive(g_log_queue.mutex);
        return -1;
    }

    /* Validate */
    if (header.syslog_len > CCU_LOG_MAX_MSG_LEN || header.uart_len > CCU_LOG_MAX_MSG_LEN) {
        g_log_queue.tail = g_log_queue.head;
        xSemaphoreGive(g_log_queue.mutex);
        return -1;
    }

    /* Read syslog data */
    char syslog_buf[CCU_LOG_MAX_MSG_LEN];
    if (header.syslog_len > 0) {
        if (!queue_read((uint8_t*)syslog_buf, header.syslog_len)) {
            xSemaphoreGive(g_log_queue.mutex);
            return -1;
        }
        syslog_buf[header.syslog_len] = '\0';
    }

    /* Read UART data */
    char uart_buf[CCU_LOG_MAX_MSG_LEN];
    if (header.uart_len > 0) {
        if (!queue_read((uint8_t*)uart_buf, header.uart_len)) {
            xSemaphoreGive(g_log_queue.mutex);
            return -1;
        }
        uart_buf[header.uart_len] = '\0';
    }

    g_log_queue.stats.popped++;
    g_log_queue.stats.current_bytes = queue_get_used();

    xSemaphoreGive(g_log_queue.mutex);

    /* Flush to outputs */
    if (header.syslog_len > 0) {
        syslog_severity_t severity;
        switch (header.level) {
            case LOG_LEVEL_ERROR: severity = SYSLOG_SEV_ERROR; break;
            case LOG_LEVEL_WARN:  severity = SYSLOG_SEV_WARNING; break;
            case LOG_LEVEL_INFO:  severity = SYSLOG_SEV_INFO; break;
            case LOG_LEVEL_DEBUG:
            default: severity = SYSLOG_SEV_DEBUG; break;
        }
        Syslog_Send(SYSLOG_FAC_LOCAL0, severity, NULL, "%s", syslog_buf);
    }

    if (header.uart_len > 0) {
        DebugP_log("%s\r\n", uart_buf);
    }

    return 0;
}

/**
 * @brief Logger task - flushes log queue to UART and Syslog
 */
static void logger_task(void *args)
{
    (void)args;

    DebugP_log("[Core0] Logger task started\r\n");

    uint32_t log_count = 0;
    const uint32_t STATS_INTERVAL = 5000;

    while (1) {
        int ret = logger_pop_and_flush(100);

        if (ret == 0) {
            log_count++;
        } else if (ret == -1) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (log_count >= STATS_INTERVAL) {
            DebugP_log("[Core0] Logger: pushed=%u, popped=%u, dropped=%u, usage=%u/%u\r\n",
                       g_log_queue.stats.pushed, g_log_queue.stats.popped,
                       g_log_queue.stats.dropped, g_log_queue.stats.current_bytes,
                       CCU_LOG_QUEUE_SIZE);
            log_count = 0;
        }
    }
}

/**
 * @brief Start logger task (called from main.c)
 */
int ccu_log_start_logger_task(void)
{
    if (gLoggerTask != NULL) {
        return 0;  /* Already started */
    }

    gLoggerTask = xTaskCreateStatic(
        logger_task,
        "Logger",
        LOGGER_TASK_SIZE,
        NULL,
        LOGGER_TASK_PRI,
        gLoggerTaskStack,
        &gLoggerTaskObj
    );

    return (gLoggerTask != NULL) ? 0 : -1;
}

void ccu_log_queue_get_stats(ccu_log_queue_stats_t* stats)
{
    if (stats && g_log_queue.queue_initialized) {
        memcpy(stats, &g_log_queue.stats, sizeof(ccu_log_queue_stats_t));
    }
}

void ccu_log_queue_reset_stats(void)
{
    if (g_log_queue.queue_initialized) {
        memset(&g_log_queue.stats, 0, sizeof(g_log_queue.stats));
    }
}

uint32_t ccu_log_queue_get_usage(void)
{
    if (g_log_queue.queue_initialized) {
        return queue_get_used();
    }
    return 0;
}
