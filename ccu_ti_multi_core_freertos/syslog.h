/*
 * syslog.h - Syslog client implementation for lwIP
 * Based on RFC 5424 (The Syslog Protocol)
 */

#ifndef __SYSLOG_H_
#define __SYSLOG_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Syslog configuration defaults */
#define SYSLOG_DEFAULT_PORT         1514          /* Standard syslog port */
#define SYSLOG_DEFAULT_REMOTE_IP    "192.168.1.3" /* Default syslog server IP */
#define SYSLOG_MAX_MSG_LEN          480          /* Max syslog message length (RFC 5424 recommends < 480) */
#define SYSLOG_HOSTNAME_MAX_LEN     32
#define SYSLOG_APP_NAME_MAX_LEN     32
#define SYSLOG_MAX_LOGS_IN_QUEUE    64           /* Max pending logs in queue */

/* Syslog facilities (RFC 5424) */
typedef enum {
    SYSLOG_FAC_KERNEL      = 0,    /* kernel messages */
    SYSLOG_FAC_USER        = 1,    /* user-level messages */
    SYSLOG_FAC_MAIL        = 2,    /* mail system */
    SYSLOG_FAC_DAEMON      = 3,    /* system daemons */
    SYSLOG_FAC_AUTH        = 4,    /* security/authorization messages */
    SYSLOG_FAC_SYSLOG      = 5,    /* messages generated internally by syslogd */
    SYSLOG_FAC_LPR         = 6,    /* line printer subsystem */
    SYSLOG_FAC_NEWS        = 7,    /* network news subsystem */
    SYSLOG_FAC_UUCP        = 8,    /* UUCP subsystem */
    SYSLOG_FAC_CRON        = 9,    /* clock daemon */
    SYSLOG_FAC_AUTHPRIV    = 10,   /* security/authorization messages (private) */
    SYSLOG_FAC_FTP         = 11,   /* FTP daemon */
    SYSLOG_FAC_NTP         = 12,   /* NTP subsystem */
    SYSLOG_FAC_LOG_AUDIT   = 13,   /* log audit */
    SYSLOG_FAC_LOG_ALERT   = 14,   /* log alert */
    SYSLOG_FAC_CLOCK       = 15,   /* clock daemon */
    SYSLOG_FAC_LOCAL0      = 16,   /* local use 0 */
    SYSLOG_FAC_LOCAL1      = 17,   /* local use 1 */
    SYSLOG_FAC_LOCAL2      = 18,   /* local use 2 */
    SYSLOG_FAC_LOCAL3      = 19,   /* local use 3 */
    SYSLOG_FAC_LOCAL4      = 20,   /* local use 4 */
    SYSLOG_FAC_LOCAL5      = 21,   /* local use 5 */
    SYSLOG_FAC_LOCAL6      = 22,   /* local use 6 */
    SYSLOG_FAC_LOCAL7      = 23    /* local use 7 */
} syslog_facility_t;

/* Syslog severities (RFC 5424) */
typedef enum {
    SYSLOG_SEV_EMERGENCY   = 0,    /* Emergency: system is unusable */
    SYSLOG_SEV_ALERT       = 1,    /* Alert: action must be taken immediately */
    SYSLOG_SEV_CRITICAL    = 2,    /* Critical: critical conditions */
    SYSLOG_SEV_ERROR       = 3,    /* Error: error conditions */
    SYSLOG_SEV_WARNING     = 4,    /* Warning: warning conditions */
    SYSLOG_SEV_NOTICE      = 5,    /* Notice: normal but significant condition */
    SYSLOG_SEV_INFO        = 6,    /* Informational: informational messages */
    SYSLOG_SEV_DEBUG       = 7     /* Debug: debug-level messages */
} syslog_severity_t;

/* Syslog configuration structure */
typedef struct {
    uint8_t  remote_ip[4];        /* Syslog server IP address (bytes) */
    uint16_t remote_port;         /* Syslog server port */
    uint8_t  facility;            /* Default facility code */
    uint8_t  min_severity;        /* Minimum severity to log (0-7) */
    bool     enabled;             /* Syslog enabled flag */
    bool     include_hostname;    /* Include hostname in message */
    bool     include_app_name;    /* Include app name in message */
    bool     include_timestamp;   /* Include timestamp in message */
    char     hostname[SYSLOG_HOSTNAME_MAX_LEN];    /* Hostname */
    char     app_name[SYSLOG_APP_NAME_MAX_LEN];    /* App name */
} syslog_config_t;

/* Syslog statistics */
typedef struct {
    uint32_t logs_sent;           /* Total logs sent successfully */
    uint32_t logs_failed;         /* Total logs failed to send */
    uint32_t logs_dropped;        /* Total logs dropped (queue full) */
    uint32_t bytes_sent;          /* Total bytes sent */
    uint32_t queue_overflow;      /* Queue overflow count */
} syslog_stats_t;

/* Default configuration macro - can be overridden in lwipcfg.h */
#ifndef SYSLOG_CONFIG
#define SYSLOG_CONFIG { \
    .remote_ip = {192, 168, 1, 3}, \
    .remote_port = SYSLOG_DEFAULT_PORT, \
    .facility = SYSLOG_FAC_LOCAL0, \
    .min_severity = SYSLOG_SEV_DEBUG, \
    .enabled = true, \
    .include_hostname = true, \
    .include_app_name = true, \
    .include_timestamp = true, \
    .hostname = "ccu-am263p", \
    .app_name = "ccu_ti" \
}
#endif

/*
 * Initialize the syslog client
 * Must be called after lwIP stack is initialized
 *
 * @param config  Pointer to configuration structure (NULL for default)
 * @return        0 on success, -1 on error
 */
int32_t Syslog_Init(const syslog_config_t *config);

/*
 * Deinitialize the syslog client
 */
void Syslog_Deinit(void);

/*
 * Send a syslog message with specified facility and severity
 *
 * @param facility  Facility code
 * @param severity  Severity level
 * @param tag       Log tag/module name (can be NULL)
 * @param fmt       Printf-style format string
 * @param ...       Variable arguments
 * @return          0 on success, -1 on error
 */
int32_t Syslog_Send(syslog_facility_t facility, syslog_severity_t severity,
                    const char *tag, const char *fmt, ...);

/*
 * Send a syslog message with default facility
 *
 * @param severity  Severity level
 * @param tag       Log tag/module name (can be NULL)
 * @param fmt       Printf-style format string
 * @param ...       Variable arguments
 * @return          0 on success, -1 on error
 */
int32_t Syslog_Log(syslog_severity_t severity, const char *tag,
                    const char *fmt, ...);

/*
 * Check if syslog is enabled
 *
 * @return  true if enabled, false otherwise
 */
bool Syslog_IsEnabled(void);

/*
 * Enable or disable syslog
 *
 * @param enable  true to enable, false to disable
 */
void Syslog_SetEnabled(bool enable);

/*
 * Get syslog statistics
 *
 * @param stats  Pointer to stats structure to fill
 */
void Syslog_GetStats(syslog_stats_t *stats);

/*
 * Reset syslog statistics
 */
void Syslog_ResetStats(void);

/*
 * Set syslog server IP address
 *
 * @param ip  IP address as 4 bytes (e.g., {192, 168, 1, 1})
 */
void Syslog_SetRemoteIP(const uint8_t ip[4]);

/*
 * Set syslog server port
 *
 * @param port  Port number
 */
void Syslog_SetRemotePort(uint16_t port);

/*
 * Set minimum severity level to log
 *
 * @param severity  Minimum severity (0-7)
 */
void Syslog_SetMinSeverity(syslog_severity_t severity);

/*
 * Convenience macros for common log levels
 */
#define Syslog_Emerg(tag, fmt, ...)   Syslog_Log(SYSLOG_SEV_EMERGENCY, tag, fmt, ##__VA_ARGS__)
#define Syslog_Alert(tag, fmt, ...)   Syslog_Log(SYSLOG_SEV_ALERT, tag, fmt, ##__VA_ARGS__)
#define Syslog_Crit(tag, fmt, ...)    Syslog_Log(SYSLOG_SEV_CRITICAL, tag, fmt, ##__VA_ARGS__)
#define Syslog_Error(tag, fmt, ...)   Syslog_Log(SYSLOG_SEV_ERROR, tag, fmt, ##__VA_ARGS__)
#define Syslog_Warn(tag, fmt, ...)    Syslog_Log(SYSLOG_SEV_WARNING, tag, fmt, ##__VA_ARGS__)
#define Syslog_Notice(tag, fmt, ...)  Syslog_Log(SYSLOG_SEV_NOTICE, tag, fmt, ##__VA_ARGS__)
#define Syslog_Info(tag, fmt, ...)    Syslog_Log(SYSLOG_SEV_INFO, tag, fmt, ##__VA_ARGS__)
#define Syslog_Debug(tag, fmt, ...)   Syslog_Log(SYSLOG_SEV_DEBUG, tag, fmt, ##__VA_ARGS__)

/*
 * Combined logging macros - output to both UART (DebugP_log) and Syslog
 * These provide a drop-in replacement for DebugP_log with syslog support
 */
#ifndef LOG_TAG
#define LOG_TAG "APP"  /* Default log tag */
#endif

#define LOG_EMERG(fmt, ...)   do { DebugP_log("[EMERG] " fmt "\r\n", ##__VA_ARGS__); \
                                   Syslog_Emerg(LOG_TAG, fmt, ##__VA_ARGS__); } while(0)
#define LOG_ALERT(fmt, ...)   do { DebugP_log("[ALERT] " fmt "\r\n", ##__VA_ARGS__); \
                                   Syslog_Alert(LOG_TAG, fmt, ##__VA_ARGS__); } while(0)
#define LOG_CRIT(fmt, ...)    do { DebugP_log("[CRIT] " fmt "\r\n", ##__VA_ARGS__); \
                                   Syslog_Crit(LOG_TAG, fmt, ##__VA_ARGS__); } while(0)
#define LOG_ERROR(fmt, ...)   do { DebugP_log("[ERROR] " fmt "\r\n", ##__VA_ARGS__); \
                                   Syslog_Error(LOG_TAG, fmt, ##__VA_ARGS__); } while(0)
#define LOG_WARN(fmt, ...)    do { DebugP_log("[WARN] " fmt "\r\n", ##__VA_ARGS__); \
                                   Syslog_Warn(LOG_TAG, fmt, ##__VA_ARGS__); } while(0)
#define LOG_NOTICE(fmt, ...)  do { DebugP_log("[NOTICE] " fmt "\r\n", ##__VA_ARGS__); \
                                   Syslog_Notice(LOG_TAG, fmt, ##__VA_ARGS__); } while(0)
#define LOG_INFO(fmt, ...)    do { DebugP_log("[INFO] " fmt "\r\n", ##__VA_ARGS__); \
                                   Syslog_Info(LOG_TAG, fmt, ##__VA_ARGS__); } while(0)
#define LOG_DEBUG(fmt, ...)   do { DebugP_log("[DEBUG] " fmt "\r\n", ##__VA_ARGS__); \
                                   Syslog_Debug(LOG_TAG, fmt, ##__VA_ARGS__); } while(0)

/* Generic log macro - uses INFO level by default */
#define LOG(fmt, ...)         LOG_INFO(fmt, ##__VA_ARGS__)

/*
 * Optional: Redirect DebugP_log to also send to syslog
 * Define SYSLOG_REDIRECT_DEBUGP to enable this feature
 */
#ifdef SYSLOG_REDIRECT_DEBUGP
  /* Warning: This will modify all DebugP_log calls throughout the codebase */
  /* Only enable if you want all DebugP_log messages sent to syslog */
  #define DebugP_log_redirect_to_syslog 1
#endif

#ifdef __cplusplus
}
#endif

#endif /* __SYSLOG_H_ */
