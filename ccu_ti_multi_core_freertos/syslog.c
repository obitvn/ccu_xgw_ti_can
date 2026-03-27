

#include "syslog.h"
#include <kernel/dpl/DebugP.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/err.h"

/* Syslog state */
static struct {
    int sock;
    bool initialized;
    syslog_config_t config;
    syslog_stats_t stats;
    uint32_t sequence;  /* Message sequence number */
} g_syslog = {
    .sock = -1,
    .initialized = false,
    .config = SYSLOG_CONFIG,
    .stats = {0},
    .sequence = 0
};

/* Default configuration */
static const syslog_config_t default_config = SYSLOG_CONFIG;

/*
 * Create RFC 5424 syslog message format
 * Format: <PRIVAL>VERSION ISOTIMESTAMP HOSTNAME APP-NAME PROCID MSGID STRUCTURED-DATA MSG
 *
 * Simplified format for embedded use:
 * <PRIVAL>ISOTIMESTAMP HOSTNAME APP_NAME TAG: MESSAGE
 */
static int format_syslog_message(char *buf, size_t buf_len,
                                  syslog_facility_t facility, syslog_severity_t severity,
                                  const char *tag, const char *msg)
{
    int pri = (facility * 8) + severity;
    int offset = 0;
    uint32_t now_ms;

    /* Get current time (using lwIP sys_now) */
    now_ms = sys_now();

    /* Start with PRI - Priority value */
    offset += snprintf(buf + offset, buf_len - offset, "<%d>", pri);

    /* Add timestamp (simplified ISO 8601 format without timezone) */
    if (g_syslog.config.include_timestamp) {
        uint32_t seconds = now_ms / 1000;
        uint32_t milliseconds = now_ms % 1000;
        offset += snprintf(buf + offset, buf_len - offset,
                          "%u.%03u ", seconds, milliseconds);
    }

    /* Add hostname */
    if (g_syslog.config.include_hostname) {
        offset += snprintf(buf + offset, buf_len - offset, "%s ",
                          g_syslog.config.hostname);
    }

    /* Add app name */
    if (g_syslog.config.include_app_name) {
        offset += snprintf(buf + offset, buf_len - offset, "%s ",
                          g_syslog.config.app_name);
    }

    /* Add tag if provided */
    if (tag != NULL && tag[0] != '\0') {
        offset += snprintf(buf + offset, buf_len - offset, "[%s] ", tag);
    }

    /* Add message */
    offset += snprintf(buf + offset, buf_len - offset, "%s", msg);

    return offset;
}

/*
 * Initialize syslog client
 */
int32_t Syslog_Init(const syslog_config_t *config)
{
    int err;

    if (g_syslog.initialized) {
        return 0;
    }

    /* Copy configuration */
    if (config != NULL) {
        memcpy(&g_syslog.config, config, sizeof(syslog_config_t));
    } else {
        memcpy(&g_syslog.config, &default_config, sizeof(syslog_config_t));
    }

    /* Create UDP socket */
    g_syslog.sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_syslog.sock < 0) {
        DebugP_log("[SYSLOG] Failed to create socket (lwIP not ready)\r\n");
        return -1;
    }

    /* Bind socket to any local port (required for lwIP UDP) */
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(0);  /* Any ephemeral port */

    lwip_bind(g_syslog.sock, (struct sockaddr *)&local_addr, sizeof(local_addr));

    /* Clear statistics */
    memset(&g_syslog.stats, 0, sizeof(syslog_stats_t));
    g_syslog.sequence = 0;

    g_syslog.initialized = true;

    DebugP_log("[SYSLOG] Initialized - Server: %u.%u.%u.%u:%d\r\n",
            g_syslog.config.remote_ip[0],
            g_syslog.config.remote_ip[1],
            g_syslog.config.remote_ip[2],
            g_syslog.config.remote_ip[3],
            g_syslog.config.remote_port);

    return 0;
}

/*
 * Deinitialize syslog client
 */
void Syslog_Deinit(void)
{
    if (g_syslog.sock >= 0) {
        close(g_syslog.sock);
        g_syslog.sock = -1;
    }

    g_syslog.initialized = false;

    DebugP_log("[SYSLOG] Deinitialized\r\n");
}

/*
 * Send syslog message
 */
static int32_t syslog_send_internal(syslog_facility_t facility, syslog_severity_t severity,
                                     const char *tag, const char *msg)
{
    char msg_buf[SYSLOG_MAX_MSG_LEN + 64]; /* Extra space for header */
    struct sockaddr_in to;
    socklen_t tolen;
    int msg_len;
    int sent;

    /* Check if enabled */
    if (!g_syslog.config.enabled) {
        return 0;
    }

    /* Check minimum severity
     * RFC 5424: 0=EMERG(highest), 7=DEBUG(lowest)
     * Log only if severity is MORE severe than or equal to min_severity
     * i.e., severity value must be <= min_severity value */
    if (severity > g_syslog.config.min_severity) {
        return 0;
    }

    /* Check socket */
    if (g_syslog.sock < 0) {
        g_syslog.stats.logs_failed++;
        return -1;
    }

    /* Format message */
    msg_len = format_syslog_message(msg_buf, sizeof(msg_buf), facility, severity, tag, msg);
    if (msg_len < 0 || msg_len > (int)sizeof(msg_buf)) {
        g_syslog.stats.logs_failed++;
        return -1;
    }

    /* Set up destination address */
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(g_syslog.config.remote_port);
    to.sin_addr.s_addr = htonl((g_syslog.config.remote_ip[0] << 24) |
                               (g_syslog.config.remote_ip[1] << 16) |
                               (g_syslog.config.remote_ip[2] << 8) |
                                g_syslog.config.remote_ip[3]);
    tolen = sizeof(to);

    /* Send message */
    sent = lwip_sendto(g_syslog.sock, msg_buf, msg_len, 0,
                       (struct sockaddr *)&to, tolen);

    if (sent < 0) {
        g_syslog.stats.logs_failed++;
        /* Only log error occasionally to avoid spam */
        if (g_syslog.stats.logs_failed % 100 == 1) {
            DebugP_log("[SYSLOG] Send failed: errno=%d\r\n", errno);
        }
        return -1;
    } else if (sent != msg_len) {
        /* Partial send - count as failed */
        g_syslog.stats.logs_failed++;
        return -1;
    }

    g_syslog.stats.logs_sent++;
    g_syslog.stats.bytes_sent += sent;
    g_syslog.sequence++;

    return 0;
}

/*
 * Public API: Send syslog message with facility and severity
 */
int32_t Syslog_Send(syslog_facility_t facility, syslog_severity_t severity,
                    const char *tag, const char *fmt, ...)
{
    va_list args;
    char msg_buf[SYSLOG_MAX_MSG_LEN + 1];
    int ret;

    if (!g_syslog.initialized) {
        return -1;
    }

    /* Format the message */
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    /* Ensure null termination */
    msg_buf[sizeof(msg_buf) - 1] = '\0';

    /* Send the message */
    ret = syslog_send_internal(facility, severity, tag, msg_buf);

    return ret;
}

/*
 * Public API: Log message with default facility
 */
int32_t Syslog_Log(syslog_severity_t severity, const char *tag,
                    const char *fmt, ...)
{
    va_list args;
    char msg_buf[SYSLOG_MAX_MSG_LEN + 1];
    int ret;

    if (!g_syslog.initialized) {
        return -1;
    }

    /* Format the message */
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    /* Ensure null termination */
    msg_buf[sizeof(msg_buf) - 1] = '\0';

    /* Send the message */
    ret = syslog_send_internal(g_syslog.config.facility, severity, tag, msg_buf);

    return ret;
}

/*
 * Check if syslog is enabled
 */
bool Syslog_IsEnabled(void)
{
    return g_syslog.config.enabled && g_syslog.initialized;
}

/*
 * Enable or disable syslog
 */
void Syslog_SetEnabled(bool enable)
{
    g_syslog.config.enabled = enable;
}

/*
 * Get syslog statistics
 */
void Syslog_GetStats(syslog_stats_t *stats)
{
    if (stats != NULL) {
        memcpy(stats, &g_syslog.stats, sizeof(syslog_stats_t));
    }
}

/*
 * Reset syslog statistics
 */
void Syslog_ResetStats(void)
{
    memset(&g_syslog.stats, 0, sizeof(syslog_stats_t));
}

/*
 * Set syslog server IP address
 */
void Syslog_SetRemoteIP(const uint8_t ip[4])
{
    if (ip != NULL) {
        memcpy(g_syslog.config.remote_ip, ip, 4);
    }
}

/*
 * Set syslog server port
 */
void Syslog_SetRemotePort(uint16_t port)
{
    g_syslog.config.remote_port = port;
}

/*
 * Set minimum severity level
 */
void Syslog_SetMinSeverity(syslog_severity_t severity)
{
    g_syslog.config.min_severity = severity;
}
