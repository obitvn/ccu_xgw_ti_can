/**
 * @file syslog.c
 * @brief Syslog client implementation for lwIP
 * Based on RFC 5424 (The Syslog Protocol)
 *
 * [FIX B114] Replace socket API with non-blocking UDP PCB to eliminate jitter
 * - Socket API (lwip_sendto) blocks via tcpip_send_msg_wait_sem()
 * - This causes 5ms jitter every 8 seconds in UDP_TX task
 * - Solution: Use direct UDP PCB with LOCK_TCPIP_CORE() like xGW UDP
 */

#include "syslog.h"
#include <kernel/dpl/DebugP.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/err.h"
#include "lwip/udp.h"
#include "lwip/tcpip.h"
#include "lwip/pbuf.h"

/* Syslog state */
static struct {
    struct udp_pcb* pcb;          /* UDP PCB (non-blocking) */
    bool initialized;
    syslog_config_t config;
    syslog_stats_t stats;
    uint32_t sequence;            /* Message sequence number */
} g_syslog = {
    .pcb = NULL,
    .initialized = false,
    .config = SYSLOG_CONFIG,
    .stats = {0},
    .sequence = 0
};

/* Default configuration */
static const syslog_config_t default_config = SYSLOG_CONFIG;

/* Forward declarations */
static int syslog_send_nonblocking(const char* msg, int msg_len);

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

    /* [FIX B107] Validate input parameters */
    if (buf == NULL || buf_len < 32) {
        return -1;
    }
    if (msg == NULL) {
        msg = "";
    }

    /* Get current time (using lwIP sys_now) */
    now_ms = sys_now();

    /* Start with PRI - Priority value */
    int written = snprintf(buf + offset, buf_len - offset, "<%d>", pri);
    if (written < 0 || written >= (int)(buf_len - offset)) {
        return -1;
    }
    offset += written;

    /* Add timestamp (simplified ISO 8601 format without timezone) */
    if (g_syslog.config.include_timestamp) {
        uint32_t seconds = now_ms / 1000;
        uint32_t milliseconds = now_ms % 1000;
        written = snprintf(buf + offset, buf_len - offset,
                          "%u.%03u ", seconds, milliseconds);
        if (written < 0 || written >= (int)(buf_len - offset)) {
            return -1;
        }
        offset += written;
    }

    /* Add hostname */
    if (g_syslog.config.include_hostname) {
        /* [FIX B105] Ensure hostname is null-terminated */
        size_t hostname_len = strnlen(g_syslog.config.hostname, 31);
        if (hostname_len == 0 || hostname_len >= 32) {
            /* Hostname corrupted - skip it */
        } else {
            written = snprintf(buf + offset, buf_len - offset, "%s ",
                              g_syslog.config.hostname);
            if (written < 0 || written >= (int)(buf_len - offset)) {
                return -1;
            }
            offset += written;
        }
    }

    /* Add app name */
    if (g_syslog.config.include_app_name) {
        /* [FIX B105] Ensure app_name is null-terminated */
        size_t app_name_len = strnlen(g_syslog.config.app_name, 31);
        if (app_name_len == 0 || app_name_len >= 32) {
            /* App name corrupted - skip it */
        } else {
            written = snprintf(buf + offset, buf_len - offset, "%s ",
                              g_syslog.config.app_name);
            if (written < 0 || written >= (int)(buf_len - offset)) {
                return -1;
            }
            offset += written;
        }
    }

    /* Add tag if provided */
    if (tag != NULL && tag[0] != '\0') {
        written = snprintf(buf + offset, buf_len - offset, "[%s] ", tag);
        if (written < 0 || written >= (int)(buf_len - offset)) {
            return -1;
        }
        offset += written;
    }

    /* Add message */
    written = snprintf(buf + offset, buf_len - offset, "%s", msg);
    if (written < 0 || written >= (int)(buf_len - offset)) {
        return -1;
    }
    offset += written;

    /* [FIX B107] Final safety check */
    if (offset < 0 || offset >= (int)buf_len) {
        return -1;
    }

    /* Ensure null termination */
    buf[offset] = '\0';

    return offset;
}

/**
 * [FIX B114] Non-blocking UDP send using UDP PCB
 * Uses LOCK_TCPIP_CORE() + udp_sendto() instead of socket API
 * This avoids the blocking tcpip_send_msg_wait_sem() call
 *
 * Implementation matches xGW UDP interface pattern:
 * - Allocate PBUF_RAM (not PBUF_ROM)
 * - Copy data into pbuf payload
 * - Send with LOCK_TCPIP_CORE()
 */
static int syslog_send_nonblocking(const char* msg, int msg_len)
{
    struct pbuf* p;
    ip_addr_t dest_addr;
    err_t err;

    /* Allocate pbuf with PBUF_RAM (same as xGW UDP interface) */
    p = pbuf_alloc(PBUF_TRANSPORT, msg_len, PBUF_RAM);
    if (p == NULL) {
        g_syslog.stats.logs_failed++;
        return -1;
    }

    /* Copy message data into pbuf payload */
    memcpy(p->payload, msg, msg_len);

    /* Set destination address */
    IP4_ADDR(&dest_addr,
             g_syslog.config.remote_ip[0],
             g_syslog.config.remote_ip[1],
             g_syslog.config.remote_ip[2],
             g_syslog.config.remote_ip[3]);

    /* Send via UDP PCB - MUST lock tcpip core */
    LOCK_TCPIP_CORE();
    err = udp_sendto(g_syslog.pcb, p, &dest_addr, g_syslog.config.remote_port);
    UNLOCK_TCPIP_CORE();

    /* Free pbuf */
    pbuf_free(p);

    if (err != ERR_OK) {
        g_syslog.stats.logs_failed++;
        return -1;
    }

    g_syslog.stats.logs_sent++;
    g_syslog.stats.bytes_sent += msg_len;
    return 0;
}

/*
 * Initialize syslog client
 */
int32_t Syslog_Init(const syslog_config_t *config)
{
    /* Copy configuration */
    if (config != NULL) {
        memcpy(&g_syslog.config, config, sizeof(syslog_config_t));
    } else {
        memcpy(&g_syslog.config, &default_config, sizeof(syslog_config_t));
    }

    /* [FIX B114] Create UDP PCB instead of socket */
    g_syslog.pcb = udp_new();
    if (g_syslog.pcb == NULL) {
        DebugP_log("[SYSLOG] Failed to create UDP PCB\r\n");
        return -1;
    }

    /* Bind to any local port */
    err_t err = udp_bind(g_syslog.pcb, IP_ADDR_ANY, 0);
    if (err != ERR_OK) {
        DebugP_log("[SYSLOG] Failed to bind UDP PCB: err=%d\r\n", err);
        udp_remove(g_syslog.pcb);
        g_syslog.pcb = NULL;
        return -1;
    }

    /* Clear statistics */
    memset(&g_syslog.stats, 0, sizeof(syslog_stats_t));
    g_syslog.sequence = 0;

    g_syslog.initialized = true;

    DebugP_log("[SYSLOG] Initialized (non-blocking) - Server: %u.%u.%u.%u:%d\r\n",
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
    if (g_syslog.pcb != NULL) {
        udp_remove(g_syslog.pcb);
        g_syslog.pcb = NULL;
    }

    g_syslog.initialized = false;

    DebugP_log("[SYSLOG] Deinitialized\r\n");
}

/*
 * Send syslog message (internal)
 */
static int32_t syslog_send_internal(syslog_facility_t facility, syslog_severity_t severity,
                                     const char *tag, const char *msg)
{
    char msg_buf[SYSLOG_MAX_MSG_LEN + 64]; /* Extra space for header */
    int msg_len;

    /* Check if enabled */
    if (!g_syslog.config.enabled) {
        return 0;
    }

    /* [FIX B105] Validate and sanitize hostname/app_name to prevent corruption */
    if (g_syslog.config.hostname[0] == '\0' ||
        g_syslog.config.hostname[SYSLOG_HOSTNAME_MAX_LEN - 1] != '\0') {
        /* Corrupted hostname - reset to default */
        const char* default_hostname = "ccu-am263p";
        strncpy((char*)g_syslog.config.hostname, default_hostname, SYSLOG_HOSTNAME_MAX_LEN - 1);
        g_syslog.config.hostname[SYSLOG_HOSTNAME_MAX_LEN - 1] = '\0';
    }
    if (g_syslog.config.app_name[0] == '\0' ||
        g_syslog.config.app_name[SYSLOG_APP_NAME_MAX_LEN - 1] != '\0') {
        /* Corrupted app_name - reset to default */
        const char* default_app_name = "ccu_ti";
        strncpy((char*)g_syslog.config.app_name, default_app_name, SYSLOG_APP_NAME_MAX_LEN - 1);
        g_syslog.config.app_name[SYSLOG_APP_NAME_MAX_LEN - 1] = '\0';
    }

    /* Check minimum severity
     * RFC 5424: 0=EMERG(highest), 7=DEBUG(lowest)
     * Log only if severity is MORE severe than or equal to min_severity
     * i.e., severity value must be <= min_severity value */
    if (severity > g_syslog.config.min_severity) {
        return 0;
    }

    /* Check PCB */
    if (g_syslog.pcb == NULL) {
        g_syslog.stats.logs_failed++;
        return -1;
    }

    /* Format message */
    msg_len = format_syslog_message(msg_buf, sizeof(msg_buf) - 1, facility, severity, tag, msg);
    if (msg_len < 0 || msg_len > (int)(sizeof(msg_buf) - 1)) {
        g_syslog.stats.logs_failed++;
        return -1;
    }

    /* Ensure null termination */
    msg_buf[msg_len] = '\0';

    /* [FIX B114] Use non-blocking UDP send */
    int ret = syslog_send_nonblocking(msg_buf, msg_len);
    if (ret != 0) {
        return -1;
    }

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

    ret = syslog_send_internal(facility, severity, tag, msg_buf);
    return ret;
}

/*
 * Send a syslog message with default facility
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
    g_syslog.sequence = 0;
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
 * Set minimum severity level to log
 */
void Syslog_SetMinSeverity(syslog_severity_t severity)
{
    g_syslog.config.min_severity = severity;
}
