/**
 * @file imu_interface_isr.c
 * @brief IMU UART ISR for TI AM263Px NoRTOS - Based on SDK example
 *
 * Reference: mcu_plus_sdk_am263px_11_01_00_19/examples/drivers/uart/uart_echo_low_latency_interrupt
 *
 * Design:
 * - UART ISR reads bytes and stores in circular buffer
 * - Protocol parsing happens in task context (main loop)
 * - No GPIO toggle in ISR (causes deadlock)
 *
 * @author Chu Tien Thinh
 * @date 2025
 */

#include "imu_interface_isr.h"
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include <kernel/dpl/HwiP.h>
#include <kernel/dpl/DebugP.h>

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define IMU_RX_BUFFER_SIZE     256     /* RX circular buffer size */

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

/**
 * @brief IMU UART state structure (based on SDK example pattern)
 */
typedef struct {
    /* UART configuration */
    uint32_t base_addr;
    uint32_t rx_trig_lvl;

    /* RX buffer */
    uint8_t  rx_buffer[IMU_RX_BUFFER_SIZE];
    volatile uint32_t rx_head;     /* Write position (ISR) */
    volatile uint32_t rx_tail;     /* Read position (task) */

    /* Statistics */
    volatile uint32_t isr_count;
    volatile uint32_t rx_byte_count;
    volatile uint32_t rx_overflow;

    /* State flags */
    volatile bool initialized;

    /* HwiP object */
    HwiP_Object hwi_object;
} imu_uart_app_t;

/*==============================================================================
 * GLOBAL VARIABLES
 *============================================================================*/

static imu_uart_app_t g_imu_uart = {0};

/* Debug counters are defined in main.c - extern declarations here */
extern volatile uint32_t dbg_imu_uart_isr_count;
extern volatile uint32_t dbg_imu_rx_byte_count;
extern volatile uint32_t dbg_imu_frame_count;

/*==============================================================================
 * FORWARD DECLARATIONS
 *============================================================================*/

static void imu_uart_isr(void *arg);

/*==============================================================================
 * PRIVATE FUNCTIONS
 *============================================================================*/

/**
 * @brief IMU UART ISR (based on SDK example App_uartUserISR)
 */
static void imu_uart_isr(void *arg)
{
    imu_uart_app_t *app = (imu_uart_app_t *)arg;
    uint32_t intr_type;
    uint8_t read_char;

    /* Increment ISR call counter */
    app->isr_count++;
    dbg_imu_uart_isr_count++;

    /* Get interrupt status */
    intr_type = UART_getIntrIdentityStatus(app->base_addr);

    /* Check RX FIFO threshold */
    if ((intr_type & UART_INTID_RX_THRES_REACH) == UART_INTID_RX_THRES_REACH) {
        /* Read all data from RX FIFO */
        while (UART_getChar(app->base_addr, &read_char) == TRUE) {
            uint32_t next_head = (app->rx_head + 1) % IMU_RX_BUFFER_SIZE;

            /* Check buffer overflow */
            if (next_head == app->rx_tail) {
                app->rx_overflow++;
            } else {
                /* Store byte in circular buffer */
                app->rx_buffer[app->rx_head] = read_char;
                app->rx_head = next_head;
                app->rx_byte_count++;
                dbg_imu_rx_byte_count++;
            }
        }
    }
}

/**
 * @brief Get number of bytes available in RX buffer
 */
static uint32_t imu_uart_rx_available(void)
{
    uint32_t head = g_imu_uart.rx_head;
    uint32_t tail = g_imu_uart.rx_tail;

    if (head >= tail) {
        return head - tail;
    } else {
        return IMU_RX_BUFFER_SIZE - tail + head;
    }
}

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * @brief Initialize IMU UART (based on SDK example App_uartInit)
 */
int imu_uart_isr_init(void)
{
    int32_t status;
    HwiP_Params hwi_params;

    DebugP_log("[IMU] Initializing UART5...\r\n");

    /* Clear state */
    memset(&g_imu_uart, 0, sizeof(g_imu_uart));

    /* Get UART base address from driver handle */
    g_imu_uart.base_addr = UART_getBaseAddr(gUartHandle[CONFIG_UART5]);
    if (g_imu_uart.base_addr == 0) {
        DebugP_logError("[IMU] ERROR: Invalid UART base address!\r\n");
        return -1;
    }

    /* Get RX trigger level from SysConfig params */
    g_imu_uart.rx_trig_lvl = gUartParams[CONFIG_UART5].rxTrigLvl;

    DebugP_log("[IMU] UART5: base=0x%08X, rxTrigLvl=%u\r\n",
               (unsigned int)g_imu_uart.base_addr, g_imu_uart.rx_trig_lvl);

    /* Register ISR using HwiP_construct (like SDK example) */
    HwiP_Params_init(&hwi_params);
    hwi_params.intNum   = gUartParams[CONFIG_UART5].intrNum;
    hwi_params.priority = gUartParams[CONFIG_UART5].intrPriority;
    hwi_params.callback = &imu_uart_isr;
    hwi_params.args     = (void *)&g_imu_uart;

    DebugP_log("[IMU] Registering ISR: intNum=%u, priority=%u\r\n",
               hwi_params.intNum, hwi_params.priority);

    status = HwiP_construct(&g_imu_uart.hwi_object, &hwi_params);
    if (status != SystemP_SUCCESS) {
        DebugP_logError("[IMU] ERROR: HwiP_construct failed! status=%d\r\n", status);
        return -1;
    }

    DebugP_log("[IMU] ISR registered successfully\r\n");

    /* Enable RX interrupt at peripheral level */
    UART_intrEnable(g_imu_uart.base_addr, UART_INTR_RHR_CTI);

    g_imu_uart.initialized = true;
    DebugP_log("[IMU] UART5 initialized (ISR mode, buffer=%u bytes)\r\n", IMU_RX_BUFFER_SIZE);

    return 0;
}

/**
 * @brief Read bytes from RX buffer (non-blocking)
 */
uint32_t imu_uart_read(uint8_t *buffer, uint32_t max_len)
{
    uint32_t count = 0;
    uint32_t key;

    if (!g_imu_uart.initialized) {
        return 0;
    }

    /* Enter critical section */
    key = HwiP_disable();

    while (count < max_len && g_imu_uart.rx_tail != g_imu_uart.rx_head) {
        buffer[count++] = g_imu_uart.rx_buffer[g_imu_uart.rx_tail];
        g_imu_uart.rx_tail = (g_imu_uart.rx_tail + 1) % IMU_RX_BUFFER_SIZE;
    }

    /* Exit critical section */
    HwiP_restore(key);

    return count;
}

/**
 * @brief Check if IMU UART is initialized
 */
bool imu_uart_is_initialized(void)
{
    return g_imu_uart.initialized;
}

/**
 * @brief Get IMU UART statistics
 */
void imu_uart_get_state(imu_uart_state_t *state)
{
    if (state == NULL) return;

    state->initialized = g_imu_uart.initialized;
    state->rx_count    = g_imu_uart.rx_byte_count;
    state->tx_count    = 0;  /* TX not implemented */
    state->error_count = g_imu_uart.rx_overflow;
}

/**
 * @brief Deinitialize IMU UART
 */
void imu_uart_deinit(void)
{
    if (g_imu_uart.initialized) {
        /* Disable RX interrupt */
        UART_intrDisable(g_imu_uart.base_addr, UART_INTR_RHR_CTI);

        /* Destruct ISR */
        HwiP_destruct(&g_imu_uart.hwi_object);

        g_imu_uart.initialized = false;
        DebugP_log("[IMU] UART5 deinitialized\r\n");
    }
}

/**
 * @brief Log ISR statistics
 */
void imu_uart_isr_log_stats(void)
{
    DebugP_log("[IMU] Stats: isr=%u, bytes=%u, avail=%u, overflow=%u\r\n",
               g_imu_uart.isr_count,
               g_imu_uart.rx_byte_count,
               imu_uart_rx_available(),
               g_imu_uart.rx_overflow);
}

/**
 * @brief Process pending IMU IPC notification (stub - can be implemented later)
 *
 * For basic IMU operation, this function returns false (no IPC notification).
 * Can be extended to send IMU data to Core0 via shared memory.
 */
bool imu_uart_process_ipc_notification(void)
{
    /* Stub - no IPC notification needed for basic IMU data logging */
    return false;
}

/*==============================================================================
 * STUB CALLBACKS (for syscfg compatibility)
 *============================================================================*/

void imu_uart_read_callback(UART_Handle handle, UART_Transaction *trans)
{
    /* Not used in ISR mode */
}

void imu_uart_write_callback(UART_Handle handle, UART_Transaction *trans)
{
    /* Not used in ISR mode */
}
