/**
 * @file gateway_shared_test.c
 * @brief Ping-Pong Buffer Test and Debug Implementation
 *
 * Test functions and logging for ping-pong buffer verification
 *
 * @author CCU Multicore Project
 * @date 2026-03-19
 */

#include "gateway_shared_test.h"
#include <kernel/dpl/DebugP.h>
#include <string.h>

/*==============================================================================
 * TEST LOG BUFFERS
 *============================================================================*/

/* Core 0 test log buffer */
static test_log_buffer_t g_test_log_c0 = {0};

/* Core 1 test log buffer */
static test_log_buffer_t g_test_log_c1 = {0};

/* Test statistics */
static test_stats_t g_test_stats_c0 = {0};
static test_stats_t g_test_stats_c1 = {0};

/* Sequence tracking */
static uint32_t g_last_seq_c0_write = 0;
static uint32_t g_last_seq_c0_read = 0;
static uint32_t g_last_seq_c1_write = 0;
static uint32_t g_last_seq_c1_read = 0;

/* Timing for latency measurement */
static uint32_t g_write_time_c0 = 0;
static uint32_t g_read_time_c0 = 0;
static uint32_t g_write_time_c1 = 0;
static uint32_t g_read_time_c1 = 0;

/*==============================================================================
 * INTERNAL HELPER FUNCTIONS
 *============================================================================*/

/**
 * @brief Get current timestamp in microseconds
 *
 * Note: This is a placeholder - actual implementation depends on timer driver
 */
static uint32_t get_timestamp_us(void)
{
    /* TODO: Implement actual timer read */
    /* For now, return a simple counter */
    static uint32_t counter = 0;
    return counter++;
}

/**
 * @brief Add log entry to buffer
 */
static void add_log_entry(test_log_buffer_t* log_buf, uint8_t core_id,
                          test_log_type_t type, uint8_t buffer_idx,
                          uint32_t sequence, uint16_t count)
{
    if (!log_buf->enabled) {
        return;
    }

    uint32_t pos = log_buf->write_pos;

    /* Check for overflow */
    if (((pos + 1) % TEST_MAX_LOG_ENTRIES) == log_buf->read_pos) {
        log_buf->overflow_count++;
        /* Overwrite oldest entry */
        log_buf->read_pos = (log_buf->read_pos + 1) % TEST_MAX_LOG_ENTRIES;
    }

    /* Fill log entry */
    log_buf->entries[pos].timestamp = get_timestamp_us();
    log_buf->entries[pos].core_id = core_id;
    log_buf->entries[pos].type = type;
    log_buf->entries[pos].buffer_idx = buffer_idx;
    log_buf->entries[pos].sequence = sequence;
    log_buf->entries[pos].count = count;

    /* Move write position */
    log_buf->write_pos = (pos + 1) % TEST_MAX_LOG_ENTRIES;
}

/**
 * @brief Update latency statistics
 */
static void update_latency(test_stats_t* stats, uint32_t latency_us)
{
    if (stats->min_latency_us == 0 || latency_us < stats->min_latency_us) {
        stats->min_latency_us = latency_us;
    }
    if (latency_us > stats->max_latency_us) {
        stats->max_latency_us = latency_us;
    }
    /* Simple running average */
    stats->avg_latency_us = (stats->avg_latency_us * 9 + latency_us) / 10;
}

/*==============================================================================
 * TEST API - Common Functions
 *============================================================================*/

int gateway_test_init(uint8_t core_id)
{
    test_log_buffer_t* log_buf = (core_id == 0) ? &g_test_log_c0 : &g_test_log_c1;
    test_stats_t* stats = (core_id == 0) ? &g_test_stats_c0 : &g_test_stats_c1;

    memset((void*)log_buf, 0, sizeof(test_log_buffer_t));
    memset((void*)stats, 0, sizeof(test_stats_t));

    log_buf->enabled = TEST_LOG_ENABLED;

    DebugP_log("[Test%d] Test logging initialized\r\n", core_id);
    return 0;
}

void gateway_test_enable(uint8_t enabled)
{
    g_test_log_c0.enabled = enabled;
    g_test_log_c1.enabled = enabled;
}

void gateway_test_clear_logs(void)
{
    g_test_log_c0.write_pos = 0;
    g_test_log_c0.read_pos = 0;
    g_test_log_c1.write_pos = 0;
    g_test_log_c1.read_pos = 0;
}

void gateway_test_reset_stats(void)
{
    memset((void*)&g_test_stats_c0, 0, sizeof(test_stats_t));
    memset((void*)&g_test_stats_c1, 0, sizeof(test_stats_t));
}

void gateway_test_get_stats(test_stats_t* stats)
{
    if (stats != NULL) {
        *stats = g_test_stats_c0;
    }
}

void gateway_test_print_stats(uint8_t core_id)
{
    const test_stats_t* stats = (core_id == 0) ? &g_test_stats_c0 : &g_test_stats_c1;
    const test_log_buffer_t* log_buf = (core_id == 0) ? &g_test_log_c0 : &g_test_log_c1;

    DebugP_log("\r\n=== Ping-Pong Test Statistics [Core %d] ===\r\n", core_id);
    DebugP_log("Operations:\r\n");
    DebugP_log("  Writes:    %u\r\n", stats->total_writes);
    DebugP_log("  Reads:     %u\r\n", stats->total_reads);
    DebugP_log("  Notifies:  %u\r\n", stats->total_notifies);
    DebugP_log("  Callbacks: %u\r\n", stats->total_callbacks);
    DebugP_log("Events:\r\n");
    DebugP_log("  BufSwitch: %u\r\n", stats->buffer_switches);
    DebugP_log("  SeqError:  %u\r\n", stats->seq_errors);
    DebugP_log("  Timeouts:  %u\r\n", stats->timeouts);
    DebugP_log("Latency (us):\r\n");
    DebugP_log("  Min: %u, Max: %u, Avg: %u\r\n",
               stats->min_latency_us, stats->max_latency_us, stats->avg_latency_us);
    DebugP_log("Log Buffer:\r\n");
    DebugP_log("  Entries: %u/%u\r\n",
               (log_buf->write_pos - log_buf->read_pos) % TEST_MAX_LOG_ENTRIES,
               TEST_MAX_LOG_ENTRIES);
    DebugP_log("  Overflow: %u\r\n", log_buf->overflow_count);
    DebugP_log("=============================================\r\n\r\n");
}

void gateway_test_dump_log(uint8_t core_id, uint32_t count)
{
    const test_log_buffer_t* log_buf = (core_id == 0) ? &g_test_log_c0 : &g_test_log_c1;
    uint32_t pos = log_buf->read_pos;
    uint32_t dumped = 0;

    DebugP_log("\r\n=== Ping-Pong Test Log [Core %d] ===\r\n", core_id);

    while (pos != log_buf->write_pos && (count == 0 || dumped < count)) {
        const test_log_entry_t* entry = &log_buf->entries[pos];

        const char* type_str = "UNKNOWN";
        switch (entry->type) {
            case TEST_LOG_WRITE:      type_str = "WRITE"; break;
            case TEST_LOG_READ:       type_str = "READ "; break;
            case TEST_LOG_NOTIFY:     type_str = "NOTIFY"; break;
            case TEST_LOG_CALLBACK:   type_str = "CALLBACK"; break;
            case TEST_LOG_BUFFER_SWITCH: type_str = "SWITCH"; break;
            case TEST_LOG_SEQ_ERROR:  type_str = "SEQ_ERR"; break;
            case TEST_LOG_TIMEOUT:    type_str = "TIMEOUT"; break;
        }

        DebugP_log("[%u] %s buf[%u] seq=%u cnt=%u\r\n",
                   entry->timestamp, type_str, entry->buffer_idx,
                   entry->sequence, entry->count);

        pos = (pos + 1) % TEST_MAX_LOG_ENTRIES;
        dumped++;
    }

    DebugP_log("=====================================\r\n\r\n");
}

/*==============================================================================
 * TEST API - Core 0 (FreeRTOS, Ethernet)
 *============================================================================*/

void gateway_test_log_write_c0(uint32_t buffer_idx, uint32_t sequence, uint16_t count)
{
    add_log_entry(&g_test_log_c0, 0, TEST_LOG_WRITE, buffer_idx, sequence, count);
    g_test_stats_c0.total_writes++;
    g_write_time_c0 = get_timestamp_us();

    /* Check sequence */
    if (g_last_seq_c0_write != 0 && sequence != g_last_seq_c0_write + 1) {
        g_test_stats_c0.seq_errors++;
        add_log_entry(&g_test_log_c0, 0, TEST_LOG_SEQ_ERROR, buffer_idx, sequence, 0);
    }
    g_last_seq_c0_write = sequence;
}

void gateway_test_log_read_c0(uint32_t buffer_idx, uint32_t sequence, uint16_t count)
{
    add_log_entry(&g_test_log_c0, 0, TEST_LOG_READ, buffer_idx, sequence, count);
    g_test_stats_c0.total_reads++;
    g_read_time_c0 = get_timestamp_us();

    /* Calculate latency */
    if (g_write_time_c0 != 0) {
        uint32_t latency = g_read_time_c0 - g_write_time_c0;
        update_latency(&g_test_stats_c0, latency);
    }

    /* Check sequence */
    if (g_last_seq_c0_read != 0 && sequence != g_last_seq_c0_read + 1) {
        g_test_stats_c0.seq_errors++;
        add_log_entry(&g_test_log_c0, 0, TEST_LOG_SEQ_ERROR, buffer_idx, sequence, 0);
    }
    g_last_seq_c0_read = sequence;
}

void gateway_test_log_notify_c0(uint16_t msg)
{
    add_log_entry(&g_test_log_c0, 0, TEST_LOG_NOTIFY, 0, msg, 0);
    g_test_stats_c0.total_notifies++;
}

void gateway_test_log_callback_c0(uint16_t msg)
{
    add_log_entry(&g_test_log_c0, 0, TEST_LOG_CALLBACK, 0, msg, 0);
    g_test_stats_c0.total_callbacks++;
}

/*==============================================================================
 * TEST API - Core 1 (NoRTOS, CAN)
 *============================================================================*/

void gateway_test_log_write_c1(uint32_t buffer_idx, uint32_t sequence, uint16_t count)
{
    add_log_entry(&g_test_log_c1, 1, TEST_LOG_WRITE, buffer_idx, sequence, count);
    g_test_stats_c1.total_writes++;
    g_write_time_c1 = get_timestamp_us();

    /* Check sequence */
    if (g_last_seq_c1_write != 0 && sequence != g_last_seq_c1_write + 1) {
        g_test_stats_c1.seq_errors++;
        add_log_entry(&g_test_log_c1, 1, TEST_LOG_SEQ_ERROR, buffer_idx, sequence, 0);
    }
    g_last_seq_c1_write = sequence;
}

void gateway_test_log_read_c1(uint32_t buffer_idx, uint32_t sequence, uint16_t count)
{
    add_log_entry(&g_test_log_c1, 1, TEST_LOG_READ, buffer_idx, sequence, count);
    g_test_stats_c1.total_reads++;
    g_read_time_c1 = get_timestamp_us();

    /* Calculate latency */
    if (g_write_time_c1 != 0) {
        uint32_t latency = g_read_time_c1 - g_write_time_c1;
        update_latency(&g_test_stats_c1, latency);
    }

    /* Check sequence */
    if (g_last_seq_c1_read != 0 && sequence != g_last_seq_c1_read + 1) {
        g_test_stats_c1.seq_errors++;
        add_log_entry(&g_test_log_c1, 1, TEST_LOG_SEQ_ERROR, buffer_idx, sequence, 0);
    }
    g_last_seq_c1_read = sequence;
}

void gateway_test_log_notify_c1(uint16_t msg)
{
    add_log_entry(&g_test_log_c1, 1, TEST_LOG_NOTIFY, 0, msg, 0);
    g_test_stats_c1.total_notifies++;
}

void gateway_test_log_callback_c1(uint16_t msg)
{
    add_log_entry(&g_test_log_c1, 1, TEST_LOG_CALLBACK, 0, msg, 0);
    g_test_stats_c1.total_callbacks++;
}

/*==============================================================================
 * TEST VERIFICATION FUNCTIONS
 *============================================================================*/

bool gateway_test_verify_buffers(uint8_t core_id)
{
    volatile GatewaySharedData_t* shared = &gGatewaySharedMem;

    if (core_id == 0) {
        /* Verify eth->can buffer */
        volatile eth_to_can_ppbuf_t* pp = &shared->eth_to_can_ppbuf;
        if (pp->write_idx >= 2 || pp->read_idx >= 2) {
            DebugP_log("[Test0] FAIL: Invalid indices\r\n");
            return false;
        }
    } else {
        /* Verify can->eth buffer */
        volatile can_to_eth_ppbuf_t* pp = &shared->can_to_eth_ppbuf;
        if (pp->write_idx >= 2 || pp->read_idx >= 2) {
            DebugP_log("[Test1] FAIL: Invalid indices\r\n");
            return false;
        }
    }

    return true;
}

uint32_t gateway_test_check_sequence_gaps(uint8_t core_id)
{
    const test_stats_t* stats = (core_id == 0) ? &g_test_stats_c0 : &g_test_stats_c1;
    return stats->seq_errors;
}

int gateway_test_run_selftest(uint8_t core_id, uint32_t iterations)
{
    DebugP_log("[Test%d] Running self-test (%u iterations)...\r\n", core_id, iterations);

    /* Reset test state */
    gateway_test_reset_stats();
    gateway_test_clear_logs();

    volatile GatewaySharedData_t* shared = &gGatewaySharedMem;

    if (core_id == 0) {
        /* Core 0 self-test: Write and verify eth->can buffer */
        volatile eth_to_can_ppbuf_t* pp = &shared->eth_to_can_ppbuf;

        for (uint32_t i = 0; i < iterations; i++) {
            uint32_t write_idx = pp->write_idx;

            /* Write test pattern */
            for (uint8_t m = 0; m < GATEWAY_NUM_MOTORS; m++) {
                pp->buffer[write_idx][m].motor_id = m;
                pp->buffer[write_idx][m].position = i;
            }

            pp->write_seq[write_idx]++;
            pp->write_done[write_idx] = 1;

            gateway_test_log_write_c0(write_idx, pp->write_seq[write_idx], GATEWAY_NUM_MOTORS);

            /* Simulate buffer switch */
            pp->write_idx = 1 - pp->write_idx;
        }

        DebugP_log("[Test0] Self-test complete\r\n");
    } else {
        /* Core 1 self-test: Write and verify can->eth buffer */
        volatile can_to_eth_ppbuf_t* pp = &shared->can_to_eth_ppbuf;

        for (uint32_t i = 0; i < iterations; i++) {
            uint32_t write_idx = pp->write_idx;

            /* Write test pattern */
            for (uint8_t m = 0; m < GATEWAY_NUM_MOTORS; m++) {
                pp->buffer[write_idx][m].motor_id = m;
                pp->buffer[write_idx][m].position = i;
            }

            pp->write_seq[write_idx]++;
            pp->write_done[write_idx] = 1;

            gateway_test_log_write_c1(write_idx, pp->write_seq[write_idx], GATEWAY_NUM_MOTORS);

            /* Simulate buffer switch */
            pp->write_idx = 1 - pp->write_idx;
        }

        DebugP_log("[Test1] Self-test complete\r\n");
    }

    /* Print results */
    gateway_test_print_stats(core_id);

    return (g_test_stats_c0.seq_errors == 0 && g_test_stats_c1.seq_errors == 0) ? 0 : -1;
}
