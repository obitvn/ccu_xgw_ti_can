/**
 * @file imu_interface.c
 * @brief IMU UART interface implementation for TI AM263Px
 *
 * Platform-specific UART interface for IMU communication
 * Uses USER_INTR mode with custom ISR for low-latency continuous RX
 * IMU data processing is done in UDP TX task (no separate IMU task)
 *
 * @author Chu Tien Thinh
 * @date 2025
 */

#include "imu_interface.h"
#include "imu_protocol_handler.h"
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include <drivers/uart.h>
#include <kernel/dpl/HwiP.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/SemaphoreP.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define IMU_DEBUG_LOG_INTERVAL  500     /* Log debug every 500ms (was 5000ms) */

/* Timing measurement */
#define IMU_ENABLE_TIMING_LOGS  1       /* Enable IMU timing logs for monitoring */

/*==============================================================================
 * TIMING STATISTICS
 *============================================================================*/

typedef struct {
    uint32_t process_max_us;       /* Max process time (microseconds) */
    uint32_t process_total_us;     /* Total process time (microseconds) */
    uint32_t process_count;        /* Process count */

    uint32_t parse_max_us;         /* Max parse time (microseconds) */
    uint32_t parse_total_us;       /* Total parse time (microseconds) */
    uint32_t parse_count;          /* Parse count */
} imu_timing_stats_t;

static imu_timing_stats_t g_timing_stats = {0};

/*==============================================================================
 * INTERNAL DATA
 *============================================================================*/

/**
 * @brief IMU UART interface state
 */
static imu_uart_state_t g_imu_uart_state = {
    .initialized = false,
    .rx_count = 0,
    .tx_count = 0,
    .error_count = 0
};

/**
 * @brief Semaphore for TX completion
 */
static SemaphoreP_Object g_imu_tx_sem;

/**
 * @brief HWI object for UART interrupt
 */
static HwiP_Object g_imu_hwi_object;

/**
 * @brief Circular RX buffer (exported for UDP TX task processing)
 */
uint8_t g_imu_rx_buffer[IMU_RX_BUFFER_SIZE];
volatile uint16_t g_imu_rx_head = 0;  /* Write position (ISR) */
volatile uint16_t g_imu_rx_tail = 0;  /* Read position (UDP TX task) */

/**
 * @brief UART base address
 */
static uint32_t g_uart_base_addr = 0;

/**
 * @brief Debug counters
 */
static uint32_t g_last_rx_count = 0;
static uint32_t g_last_log_time = 0;
static uint32_t g_isr_count = 0;
static uint32_t g_rx_overflow_count = 0;

/*==============================================================================
 * INTERNAL FUNCTIONS
 *============================================================================*/

/**
 * @brief Custom UART ISR
 *
 * Called when RX FIFO reaches trigger level
 * Reads all available data from FIFO into circular buffer
 */
static void imu_uart_isr(void* arg)
{
    (void)arg;
    uint32_t intr_type;
    uint8_t read_char;
    uint32_t read_success;

    g_isr_count++;

    /* Get interrupt status */
    intr_type = UART_getIntrIdentityStatus(g_uart_base_addr);

    /* Check RX FIFO threshold */
    if ((intr_type & UART_INTID_RX_THRES_REACH) == UART_INTID_RX_THRES_REACH) {
        /* Read all available data from RX FIFO */
        while (1) {
            read_success = UART_getChar(g_uart_base_addr, &read_char);
            if (read_success == TRUE) {
                /* Calculate next head position */
                uint16_t next_head = (g_imu_rx_head + 1) % IMU_RX_BUFFER_SIZE;

                /* Check if buffer would overflow */
                if (next_head == g_imu_rx_tail) {
                    /* Buffer overflow - discard data */
                    g_rx_overflow_count++;
                } else {
                    /* Store byte in circular buffer */
                    g_imu_rx_buffer[g_imu_rx_head] = read_char;
                    g_imu_rx_head = next_head;
                    g_imu_uart_state.rx_count++;
                }
            } else {
                /* No more data in FIFO */
                break;
            }
        }
    }

    /* TX interrupt not used in this implementation */
}

/*==============================================================================
 * PUBLIC API FUNCTIONS
 *============================================================================*/

int imu_uart_init(void)
{
    int32_t status;
    HwiP_Params hwi_params;

    if (g_imu_uart_state.initialized) {
        return 0;  /* Already initialized */
    }

    DebugP_log("[IMU] Initializing UART interface (USER_INTR mode, no RX task)...\r\n");

    /* Verify UART is opened */
    if (gUartHandle[CONFIG_UART5] == NULL) {
        DebugP_logError("[IMU] UART5 handle is NULL!\r\n");
        return -1;
    }

    /* Get UART base address */
    g_uart_base_addr = UART_getBaseAddr(gUartHandle[CONFIG_UART5]);
    if (g_uart_base_addr == 0) {
        DebugP_logError("[IMU] Invalid UART base address!\r\n");
        return -1;
    }

    DebugP_log("[IMU] UART5: base=0x%08X\r\n", (unsigned int)g_uart_base_addr);

    /* Create TX completion semaphore (not currently used but kept for future) */
    status = SemaphoreP_constructBinary(&g_imu_tx_sem, 0);
    if (status != SystemP_SUCCESS) {
        DebugP_logError("[IMU] Failed to create TX semaphore\r\n");
        return -1;
    }

    /* Register custom interrupt handler */
    HwiP_Params_init(&hwi_params);
    hwi_params.intNum = gUartParams[CONFIG_UART5].intrNum;
    hwi_params.priority = gUartParams[CONFIG_UART5].intrPriority;
    hwi_params.callback = &imu_uart_isr;
    hwi_params.args = NULL;

    status = HwiP_construct(&g_imu_hwi_object, &hwi_params);
    if (status != SystemP_SUCCESS) {
        DebugP_logError("[IMU] Failed to register ISR: %d\r\n", status);
        SemaphoreP_destruct(&g_imu_tx_sem);
        return -1;
    }

    /* Enable RX interrupt */
    UART_intrEnable(g_uart_base_addr, UART_INTR_RHR_CTI);

    g_imu_uart_state.initialized = true;
    g_last_log_time = xTaskGetTickCount();

    DebugP_log("[IMU] UART interface initialized (USER_INTR mode)\r\n");
    DebugP_log("[IMU] IMU data will be processed in UDP TX task\r\n");
    DebugP_log("[IMU] Expected YIS320 data: header 0x59 0x53 at 400Hz\r\n");

    return 0;
}

int imu_uart_send(const uint8_t* data, uint16_t length)
{
    if (!g_imu_uart_state.initialized || data == NULL || length == 0) {
        return -1;
    }

    /* Simple blocking send */
    for (uint16_t i = 0; i < length; i++) {
        UART_putChar(g_uart_base_addr, data[i]);
    }

    g_imu_uart_state.tx_count += length;
    return length;
}

bool imu_uart_is_initialized(void)
{
    return g_imu_uart_state.initialized;
}

void imu_uart_get_state(imu_uart_state_t* state)
{
    if (state != NULL) {
        *state = g_imu_uart_state;
    }
}

/**
 * @brief Process IMU data from circular buffer
 *
 * Called from IMU RX task to process any pending IMU data
 * This function processes all available bytes in the circular buffer
 *
 * OPTIMIZATION: Read all available bytes in ONE critical section
 * instead of per-byte to minimize critical section overhead
 */
void imu_uart_process_rx_data(void)
{
#if IMU_ENABLE_TIMING_LOGS
    uint64_t start_us = ClockP_getTimeUsec();
#endif

    /* Collect available data in temp buffer for batch processing
     * Larger buffer = fewer calls to protocol processor = less overhead
     * But we still limit frames per call to prevent CPU spikes */
    uint8_t temp_buf[256];  /* Increased from 128 to 256 bytes */
    uint16_t temp_len = 0;

    /* ========== CRITICAL SECTION: Read buffer state and copy data ========== */
    UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();

    uint16_t available = (g_imu_rx_head >= g_imu_rx_tail) ?
                          (g_imu_rx_head - g_imu_rx_tail) :
                          (IMU_RX_BUFFER_SIZE - g_imu_rx_tail + g_imu_rx_head);

    /* Limit to temp buffer size */
    if (available > sizeof(temp_buf)) {
        available = sizeof(temp_buf);
    }

    /* Copy ALL data in ONE critical section - much faster! */
    uint16_t to_copy = available;
    while (to_copy > 0 && g_imu_rx_tail != g_imu_rx_head) {
        temp_buf[temp_len++] = g_imu_rx_buffer[g_imu_rx_tail];
        g_imu_rx_tail = (g_imu_rx_tail + 1) % IMU_RX_BUFFER_SIZE;
        to_copy--;
    }

    taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
    /* ========== END CRITICAL SECTION ========== */

#if IMU_ENABLE_TIMING_LOGS
    uint64_t parse_start_us = ClockP_getTimeUsec();
#endif

    /* Process as batch */
    if (temp_len > 0) {
        imu_protocol_process_uart_rx(temp_buf, temp_len);
    }

#if IMU_ENABLE_TIMING_LOGS
    uint64_t parse_end_us = ClockP_getTimeUsec();
    uint32_t parse_time_us = (uint32_t)(parse_end_us - parse_start_us);
    if (parse_time_us > g_timing_stats.parse_max_us) {
        g_timing_stats.parse_max_us = parse_time_us;
    }
    g_timing_stats.parse_total_us += parse_time_us;
    g_timing_stats.parse_count++;

    uint64_t end_us = ClockP_getTimeUsec();
    uint32_t process_time_us = (uint32_t)(end_us - start_us);
    if (process_time_us > g_timing_stats.process_max_us) {
        g_timing_stats.process_max_us = process_time_us;
    }
    g_timing_stats.process_total_us += process_time_us;
    g_timing_stats.process_count++;
#endif

    /* Log statistics every 5 seconds */
    uint32_t current_time = xTaskGetTickCount();
    if ((current_time - g_last_log_time) >= pdMS_TO_TICKS(IMU_DEBUG_LOG_INTERVAL)) {
        uint32_t rx_delta = g_imu_uart_state.rx_count - g_last_rx_count;
        uint16_t buffer_usage = (g_imu_rx_head >= g_imu_rx_tail) ?
                               (g_imu_rx_head - g_imu_rx_tail) :
                               (IMU_RX_BUFFER_SIZE - g_imu_rx_tail + g_imu_rx_head);
        DebugP_log("[IMU] Stats: total_rx=%u, delta=%u, isr=%u, overflow=%u, buf=%u/%u, temp_len=%u\r\n",
                 g_imu_uart_state.rx_count, rx_delta, g_isr_count,
                 g_rx_overflow_count, buffer_usage, IMU_RX_BUFFER_SIZE, temp_len);

#if IMU_ENABLE_TIMING_LOGS
        /* Print timing statistics */
        uint32_t avg_process_us = (g_timing_stats.process_count > 0) ?
                                  (g_timing_stats.process_total_us / g_timing_stats.process_count) : 0;
        uint32_t avg_parse_us = (g_timing_stats.parse_count > 0) ?
                                (g_timing_stats.parse_total_us / g_timing_stats.parse_count) : 0;

        DebugP_log("[IMU] Timing: Process(max=%u, avg=%u us), Parse(max=%u, avg=%u us), count=%u\r\n",
                 g_timing_stats.process_max_us, avg_process_us,
                 g_timing_stats.parse_max_us, avg_parse_us,
                 g_timing_stats.process_count);

        /* Reset statistics */
        memset(&g_timing_stats, 0, sizeof(g_timing_stats));
#endif

        g_last_log_time = current_time;
        g_last_rx_count = g_imu_uart_state.rx_count;
    }
}

void imu_uart_deinit(void)
{
    if (!g_imu_uart_state.initialized) {
        return;
    }

    /* Disable interrupts */
    UART_intrDisable(g_uart_base_addr, UART_INTR_RHR_CTI);

    /* Destroy HWI */
    HwiP_destruct(&g_imu_hwi_object);

    /* Destroy semaphore */
    SemaphoreP_destruct(&g_imu_tx_sem);

    g_imu_uart_state.initialized = false;
    DebugP_log("[IMU] UART interface deinitialized\r\n");
}

/*==============================================================================
 * STUB CALLBACKS (kept for syscfg compatibility, not used in USER_INTR mode)
 *============================================================================*/

void imu_uart_read_callback(UART_Handle handle, UART_Transaction *trans)
{
    (void)handle;
    (void)trans;
    /* Not used in USER_INTR mode */
}

void imu_uart_write_callback(UART_Handle handle, UART_Transaction *trans)
{
    (void)handle;
    (void)trans;
    /* Not used in USER_INTR mode */
}

/*==============================================================================
 * YIS320 PROTOCOL PLATFORM FUNCTIONS
 *============================================================================*/

int yis320_uart_configure(uint8_t uart_port, uint32_t baud_rate)
{
    if (uart_port != IMU_UART_PORT) {
        DebugP_logError("[IMU] UART port mismatch: expected %d, got %d\r\n",
                       IMU_UART_PORT, uart_port);
        return -1;
    }

    (void)baud_rate;  /* Baudrate is configured in syscfg */

    return imu_uart_init();
}

int yis320_uart_send(uint8_t uart_port, const uint8_t* data, uint16_t length)
{
    if (uart_port != IMU_UART_PORT) {
        return -1;
    }

    return imu_uart_send(data, length);
}

uint32_t yis320_get_timestamp_ms(void)
{
    uint64_t usec = ClockP_getTimeUsec();
    return (uint32_t)(usec / 1000ULL);
}
