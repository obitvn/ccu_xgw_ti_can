/**
 * @file gateway_shared_test.h
 * @brief Ping-Pong Buffer Test and Debug Interface
 *
 * Test functions and logging for ping-pong buffer verification
 *
 * @author CCU Multicore Project
 * @date 2026-03-19
 */

#ifndef GATEWAY_SHARED_TEST_H_
#define GATEWAY_SHARED_TEST_H_

#include <stdint.h>
#include <stdbool.h>
#include "gateway_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * TEST CONFIGURATION
 *============================================================================*/

#define TEST_LOG_ENABLED          1       /* Enable test logging */
#define TEST_MAX_LOG_ENTRIES      256     /* Max log entries per buffer */

/*==============================================================================
 * TEST LOG ENTRY TYPE
 *============================================================================*/

/**
 * @brief Test log entry for tracking ping-pong operations
 */
typedef enum {
    TEST_LOG_NONE = 0,
    TEST_LOG_WRITE,           /* Write operation */
    TEST_LOG_READ,            /* Read operation */
    TEST_LOG_NOTIFY,          /* IPC notify sent */
    TEST_LOG_CALLBACK,        /* IPC callback received */
    TEST_LOG_BUFFER_SWITCH,   /* Buffer switch occurred */
    TEST_LOG_SEQ_ERROR,       /* Sequence error detected */
    TEST_LOG_TIMEOUT,         /* Buffer timeout occurred */
} test_log_type_t;

/**
 * @brief Test log entry structure
 */
typedef struct {
    uint32_t    timestamp;    /* Timestamp (cycles or us) */
    uint8_t     core_id;      /* Core ID (0 or 1) */
    uint8_t     type;         /* Log type (test_log_type_t) */
    uint8_t     buffer_idx;   /* Buffer index (0 or 1) */
    uint32_t    sequence;     /* Sequence number */
    uint16_t    count;        /* Motor count */
    uint16_t    reserved;     /* Future use */
} test_log_entry_t;

/**
 * @brief Test log buffer structure
 */
typedef struct {
    test_log_entry_t entries[TEST_MAX_LOG_ENTRIES];
    volatile uint32_t write_pos;
    volatile uint32_t read_pos;
    volatile uint32_t overflow_count;
    volatile uint32_t enabled;
} test_log_buffer_t;

/**
 * @brief Test statistics structure
 */
typedef struct {
    uint32_t    total_writes;
    uint32_t    total_reads;
    uint32_t    total_notifies;
    uint32_t    total_callbacks;
    uint32_t    buffer_switches;
    uint32_t    seq_errors;
    uint32_t    timeouts;
    uint32_t    min_latency_us;
    uint32_t    max_latency_us;
    uint32_t    avg_latency_us;
} test_stats_t;

/*==============================================================================
 * TEST API - Common Functions
 *============================================================================*/

/**
 * @brief Initialize test logging
 *
 * @param core_id Core ID (0 or 1)
 * @return 0 on success, -1 on error
 */
int gateway_test_init(uint8_t core_id);

/**
 * @brief Enable/disable test logging
 *
 * @param enabled 1 to enable, 0 to disable
 */
void gateway_test_enable(uint8_t enabled);

/**
 * @brief Clear all test logs
 */
void gateway_test_clear_logs(void);

/**
 * @brief Reset test statistics
 */
void gateway_test_reset_stats(void);

/**
 * @brief Get test statistics
 *
 * @param stats Output statistics structure
 */
void gateway_test_get_stats(test_stats_t* stats);

/**
 * @brief Print test statistics to debug console
 *
 * @param core_id Core ID (0 or 1)
 */
void gateway_test_print_stats(uint8_t core_id);

/**
 * @brief Dump test log entries to debug console
 *
 * @param core_id Core ID (0 or 1)
 * @param count Number of entries to dump (0 = all)
 */
void gateway_test_dump_log(uint8_t core_id, uint32_t count);

/*==============================================================================
 * TEST API - Core 0 (FreeRTOS, Ethernet)
 *============================================================================*/

/**
 * @brief Log write operation (Core 0 writes motor commands)
 *
 * @param buffer_idx Buffer index written
 * @param sequence Sequence number
 * @param count Number of motors
 */
void gateway_test_log_write_c0(uint32_t buffer_idx, uint32_t sequence, uint16_t count);

/**
 * @brief Log read operation (Core 0 reads motor states)
 *
 * @param buffer_idx Buffer index read
 * @param sequence Sequence number
 * @param count Number of motors
 */
void gateway_test_log_read_c0(uint32_t buffer_idx, uint32_t sequence, uint16_t count);

/**
 * @brief Log notify operation (Core 0 sends IPC notify)
 *
 * @param msg Message ID
 */
void gateway_test_log_notify_c0(uint16_t msg);

/**
 * @brief Log callback operation (Core 0 receives IPC callback)
 *
 * @param msg Message ID received
 */
void gateway_test_log_callback_c0(uint16_t msg);

/*==============================================================================
 * TEST API - Core 1 (NoRTOS, CAN)
 *============================================================================*/

/**
 * @brief Log write operation (Core 1 writes motor states)
 *
 * @param buffer_idx Buffer index written
 * @param sequence Sequence number
 * @param count Number of motors
 */
void gateway_test_log_write_c1(uint32_t buffer_idx, uint32_t sequence, uint16_t count);

/**
 * @brief Log read operation (Core 1 reads motor commands)
 *
 * @param buffer_idx Buffer index read
 * @param sequence Sequence number
 * @param count Number of motors
 */
void gateway_test_log_read_c1(uint32_t buffer_idx, uint32_t sequence, uint16_t count);

/**
 * @brief Log notify operation (Core 1 sends IPC notify)
 *
 * @param msg Message ID
 */
void gateway_test_log_notify_c1(uint16_t msg);

/**
 * @brief Log callback operation (Core 1 receives IPC callback)
 *
 * @param msg Message ID received
 */
void gateway_test_log_callback_c1(uint16_t msg);

/*==============================================================================
 * TEST VERIFICATION FUNCTIONS
 *============================================================================*/

/**
 * @brief Verify ping-pong buffer integrity
 *
 * @param core_id Core ID (0 or 1)
 * @return true if buffers are valid, false otherwise
 */
bool gateway_test_verify_buffers(uint8_t core_id);

/**
 * @brief Check for sequence gaps
 *
 * @param core_id Core ID (0 or 1)
 * @return Number of sequence gaps found
 */
uint32_t gateway_test_check_sequence_gaps(uint8_t core_id);

/**
 * @brief Run ping-pong buffer self-test
 *
 * @param core_id Core ID (0 or 1)
 * @param iterations Number of test iterations
 * @return 0 if all tests passed, -1 if any test failed
 */
int gateway_test_run_selftest(uint8_t core_id, uint32_t iterations);

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_SHARED_TEST_H_ */
