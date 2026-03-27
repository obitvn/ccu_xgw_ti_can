/**
 * @file imu_interface_isr.c
 * @brief IMU UART interface with Parse-in-ISR for TI AM263Px
 *
 * Parse YIS320 IMU frames directly in UART ISR for lowest latency.
 * Eliminates IMU RX task, achieves instant data availability.
 *
 * @author Chu Tien Thinh
 * @date 2025
 */

#include "imu_interface_isr.h"
/* Note: Not including yis320_protocol.h to avoid macro conflicts */
/* We define our own constants locally */
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include <drivers/uart.h>
#include <kernel/dpl/HwiP.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/ClockP.h>
#include "../../../gateway_shared.h"
#include <string.h>

/*==============================================================================
 * CRITICAL SECTION WRAPPERS (NoRTOS)
 *============================================================================*/
/* Use HwiP API from SDK for critical sections */
#include <kernel/dpl/HwiP.h>

#define IMU_ENTER_CRITICAL()    uintptr_t imu_cs_key = HwiP_disable()
#define IMU_EXIT_CRITICAL()     HwiP_restore(imu_cs_key)

/*==============================================================================
 * CONSTANTS
 *============================================================================*/

#define IMU_ISR_DEBUG_LOG_INTERVAL  5000     /* Log debug every 5s */

/* YIS320 Protocol Constants */
#define YIS320_HEADER_BYTE_1     0x59
#define YIS320_HEADER_BYTE_2     0x53
#define YIS320_MAX_FRAME_SIZE    256        /* Max YIS320 frame size */
#define YIS320_MAX_PAYLOAD_LEN   200        /* Max payload length */

/* Frame offsets */
#define YIS320_OFFSET_TID        2          /* TID field offset */
#define YIS320_OFFSET_LEN        4          /* Length field offset */
#define YIS320_HEADER_SIZE       5          /* Header + TID + LEN */
#define YIS320_CHECKSUM_SIZE     2          /* Checksum bytes */

/* Enable timing logs */
#define IMU_ISR_ENABLE_TIMING    1

/*==============================================================================
 * TYPES
 *============================================================================*/

/**
 * @brief IMU Parser State Machine
 */
typedef enum {
    IMU_STATE_SEARCH_HEADER = 0,    /* Waiting for 0x59 */
    IMU_STATE_VERIFY_HEADER,        /* Got 0x59, waiting for 0x53 */
    IMU_STATE_READ_LENGTH,          /* Got header, read payload length */
    IMU_STATE_READ_PAYLOAD,         /* Accumulating payload */
    IMU_STATE_READ_CHECKSUM_1,      /* Reading first checksum byte */
    IMU_STATE_READ_CHECKSUM_2,      /* Reading second checksum byte */
    IMU_STATE_VALIDATE_PARSE        /* Validate and parse frame */
} imu_parser_state_t;

/**
 * @brief ISR Parser State
 */
typedef struct {
    uint8_t     rx_buffer[YIS320_MAX_FRAME_SIZE];  /* Frame buffer */
    uint8_t     buffer_pos;                        /* Current position */
    uint8_t     payload_len;                       /* Expected payload length */
    uint8_t     checksum[2];                        /* Checksum bytes */
    imu_parser_state_t state;                      /* Parser state */
    uint32_t    error_count;                       /* Parse error counter */
    uint32_t    frame_count;                       /* Valid frame counter */
    uint32_t    byte_count;                        /* Total bytes received */
} imu_isr_parser_t;

/**
 * @brief Timing statistics for ISR
 */
typedef struct {
    uint32_t isr_max_us;
    uint32_t isr_total_us;
    uint32_t isr_count;
    uint32_t parse_max_us;
    uint32_t parse_total_us;
    uint32_t parse_count;
    uint32_t frame_count;
    uint32_t error_count;
    uint64_t last_log_time;  /* Microseconds */
} imu_isr_stats_t;

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
 * @brief HWI object for UART interrupt
 */
static HwiP_Object g_imu_hwi_object;

/**
 * @brief UART base address
 */
static uint32_t g_uart_base_addr = 0;

/**
 * @brief ISR Parser State (one per UART)
 */
static imu_isr_parser_t g_imu_parser = {
    .state = IMU_STATE_SEARCH_HEADER,
    .buffer_pos = 0,
    .payload_len = 0,
    .error_count = 0,
    .frame_count = 0,
    .byte_count = 0
};

/**
 * @brief Timing statistics
 */
static imu_isr_stats_t g_isr_stats = {0};

/**
 * @brief Shared IMU state (atomic update by ISR)
 */
static volatile struct {
    float   gyroscope[3];      /* Angular velocity [rad/s] */
    float   rpy[3];            /* Roll, Pitch, Yaw [rad] */
    float   quaternion[4];     /* Quaternion [w, x, y, z] */
    uint32_t timestamp;        /* Timestamp in milliseconds */
    volatile bool updated;     /* Flag for new data */
} g_imu_atomic_state = {0};

/*==============================================================================
 * FORWARD DECLARATIONS
 *============================================================================*/

static void reset_parser(void);
static bool validate_yis320_checksum(const uint8_t* buffer, uint8_t frame_size);
static void parse_yis320_frame(const uint8_t* buffer, uint8_t frame_size);
static void update_imu_state_atomic(const float gyro[3], const float rpy[3],
                                     const float quat[4]);

/*==============================================================================
 * INTERNAL FUNCTIONS
 *============================================================================*/

/**
 * @brief Reset parser to initial state
 */
static void reset_parser(void)
{
    g_imu_parser.state = IMU_STATE_SEARCH_HEADER;
    g_imu_parser.buffer_pos = 0;
    g_imu_parser.payload_len = 0;
    g_imu_parser.checksum[0] = 0;
    g_imu_parser.checksum[1] = 0;
}

/**
 * @brief Validate YIS320 checksum
 *
 * Checksum: CK1 (LSB) + CK2 (MSB) accumulated from TID to end of payload
 *
 * @param buffer Frame buffer
 * @param frame_size Frame size including checksum
 * @return true if checksum valid
 */
static bool validate_yis320_checksum(const uint8_t* buffer, uint8_t frame_size)
{
    if (frame_size < 7) {
        return false;
    }

    /* Calculate checksum from TID (byte 2) to end of payload */
    uint8_t ck1 = 0, ck2 = 0;
    for (int i = 2; i < frame_size - 2; i++) {
        ck1 += buffer[i];
        ck2 += ck1;
    }

    /* Received checksum */
    uint16_t received = ((uint16_t)buffer[frame_size - 1] << 8) |
                        buffer[frame_size - 2];
    uint16_t calculated = ((uint16_t)ck2 << 8) | ck1;

    return (received == calculated);
}

/**
 * @brief Parse 32-bit signed integer (little-endian)
 */
static inline int32_t parse_int32_le(const uint8_t* data)
{
    return (int32_t)((data[3] << 24) | (data[2] << 16) |
                     (data[1] << 8) | data[0]);
}

/**
 * @brief Parse angular velocity (gyroscope) - Data ID 0x20
 */
static void parse_gyroscope(const uint8_t* data, float* gyro_out)
{
    /* Use local constants with different names to avoid macro conflict */
    const float data_factor = 0.000001f;
    const float deg_to_rad = 0.017453292519943295f;

    for (int i = 0; i < 3; i++) {
        int32_t raw = parse_int32_le(data + i * 4);
        gyro_out[i] = (float)raw * data_factor * deg_to_rad;
    }
}

/**
 * @brief Parse Euler angles - Data ID 0x40
 */
static void parse_euler_angles(const uint8_t* data, float* rpy_out)
{
    /* Use local constants with different names to avoid macro conflict */
    const float data_factor = 0.000001f;
    const float deg_to_rad = 0.017453292519943295f;

    for (int i = 0; i < 3; i++) {
        int32_t raw = parse_int32_le(data + i * 4);
        rpy_out[i] = (float)raw * data_factor * deg_to_rad;
    }
}

/**
 * @brief Parse quaternion - Data ID 0x41
 */
static void parse_quaternion(const uint8_t* data, float* quat_out)
{
    /* Use local constants with different names to avoid macro conflict */
    const float data_factor = 0.000001f;

    for (int i = 0; i < 4; i++) {
        int32_t raw = parse_int32_le(data + i * 4);
        quat_out[i] = (float)raw * data_factor;
    }
}

/**
 * @brief Parse complete YIS320 frame
 *
 * @param buffer Frame buffer
 * @param frame_size Frame size
 */
static void parse_yis320_frame(const uint8_t* buffer, uint8_t frame_size)
{
    /* Temporary storage for parsed data */
    float gyro[3] = {0};
    float rpy[3] = {0};
    float quat[4] = {0};
    bool has_gyro = false;
    bool has_rpy = false;
    bool has_quat = false;

    /* Extract payload length */
    uint8_t payload_len = buffer[YIS320_OFFSET_LEN];

    /* Parse data items */
    int offset = YIS320_HEADER_SIZE;
    int end_offset = YIS320_HEADER_SIZE + payload_len;

    while (offset < end_offset) {
        if (offset + 2 > frame_size - 2) {
            break;  /* Not enough for data_id + data_len */
        }

        uint8_t data_id = buffer[offset];
        uint8_t data_len = buffer[offset + 1];

        if (offset + 2 + data_len > frame_size - 2) {
            break;  /* Data item doesn't fit */
        }

        switch (data_id) {
            case 0x20:  /* Gyroscope */
                if (data_len == 12) {
                    parse_gyroscope(buffer + offset + 2, gyro);
                    has_gyro = true;
                }
                break;

            case 0x40:  /* Euler angles */
                if (data_len == 12) {
                    parse_euler_angles(buffer + offset + 2, rpy);
                    has_rpy = true;
                }
                break;

            case 0x41:  /* Quaternion */
                if (data_len == 16) {
                    parse_quaternion(buffer + offset + 2, quat);
                    has_quat = true;
                }
                break;

            default:
                /* Unknown data type, skip */
                break;
        }

        offset += 2 + data_len;
    }

    /* Update atomic state if we have at least one valid data type */
    if (has_gyro || has_rpy || has_quat) {
        update_imu_state_atomic(gyro, rpy, quat);
    }
}

/**
 * @brief Update IMU state atomically (critical section)
 *
 * [MIGRATED FROM draft/ccu_ti/imu/imu_interface_isr.c:336]
 * [MODIFIED: Added IPC integration for Core1 -> Core0 communication]
 */
static void update_imu_state_atomic(const float gyro[3], const float rpy[3],
                                     const float quat[4])
{
    IMU_ENTER_CRITICAL();

    /* Copy data to local atomic state */
    memcpy((void*)g_imu_atomic_state.gyroscope, gyro, sizeof(g_imu_atomic_state.gyroscope));
    memcpy((void*)g_imu_atomic_state.rpy, rpy, sizeof(g_imu_atomic_state.rpy));
    memcpy((void*)g_imu_atomic_state.quaternion, quat, sizeof(g_imu_atomic_state.quaternion));

    /* Update timestamp */
    g_imu_atomic_state.timestamp = (uint32_t)(ClockP_getTimeUsec() / 1000ULL);
    g_imu_atomic_state.updated = true;

    /* ========== IPC INTEGRATION: Write to shared memory for Core0 ========== */
    /* [NEW: Added for multicore communication] */
    imu_state_ipc_t ipc_imu = {0};
    ipc_imu.imu_id = 0;  /* Default IMU ID */
    ipc_imu.temp_cdeg = 0;  /* Temperature not available in ISR */
    memcpy(ipc_imu.gyro, gyro, sizeof(ipc_imu.gyro));
    memcpy(ipc_imu.quat, quat, sizeof(ipc_imu.quat));
    memcpy(ipc_imu.euler, rpy, sizeof(ipc_imu.euler));
    /* mag_val and mag_norm left as 0.0f */

    gateway_write_imu_state(&ipc_imu);
    /* ========== END IPC INTEGRATION ========== */

    IMU_EXIT_CRITICAL();

    /* Notify Core0 after critical section (avoid deadlock) */
    gateway_notify_imu_ready();
}

/*==============================================================================
 * UART ISR - PARSE IN ISR
 *============================================================================*/

/**
 * @brief UART ISR with integrated YIS320 parser
 *
 * This ISR processes incoming UART data byte-by-byte and parses
 * YIS320 frames directly in interrupt context for lowest latency.
 */
static void imu_uart_isr(void* arg)
{
    (void)arg;
#if IMU_ISR_ENABLE_TIMING
    uint64_t start_us = ClockP_getTimeUsec();
#endif

    uint32_t intr_type;
    uint8_t read_char;
    uint32_t read_success;

    /* Get interrupt status */
    intr_type = UART_getIntrIdentityStatus(g_uart_base_addr);

    /* Check RX FIFO threshold */
    if ((intr_type & UART_INTID_RX_THRES_REACH) == UART_INTID_RX_THRES_REACH) {

        /* Read all available data from RX FIFO */
        while (1) {
            read_success = UART_getChar(g_uart_base_addr, &read_char);
            if (read_success != TRUE) {
                break;  /* No more data */
            }

            g_imu_parser.byte_count++;

            /* State machine for parsing */
            switch (g_imu_parser.state) {

                case IMU_STATE_SEARCH_HEADER:
                    if (read_char == YIS320_HEADER_BYTE_1) {
                        g_imu_parser.rx_buffer[0] = read_char;
                        g_imu_parser.buffer_pos = 1;
                        g_imu_parser.state = IMU_STATE_VERIFY_HEADER;
                    }
                    break;

                case IMU_STATE_VERIFY_HEADER:
                    if (read_char == YIS320_HEADER_BYTE_2) {
                        g_imu_parser.rx_buffer[1] = read_char;
                        g_imu_parser.buffer_pos = 2;
                        g_imu_parser.state = IMU_STATE_READ_LENGTH;
                    } else {
                        /* Not valid header, check if this is new header byte 1 */
                        if (read_char == YIS320_HEADER_BYTE_1) {
                            g_imu_parser.rx_buffer[0] = read_char;
                            g_imu_parser.buffer_pos = 1;
                            /* Stay in VERIFY_HEADER (actually wait for byte 2) */
                        } else {
                            reset_parser();
                        }
                    }
                    break;

                case IMU_STATE_READ_LENGTH:
                    /* Read TID (bytes 2-3) and LEN (byte 4) */
                    g_imu_parser.rx_buffer[g_imu_parser.buffer_pos++] = read_char;

                    if (g_imu_parser.buffer_pos >= 5) {
                        g_imu_parser.payload_len = g_imu_parser.rx_buffer[YIS320_OFFSET_LEN];

                        /* Validate payload length */
                        if (g_imu_parser.payload_len > YIS320_MAX_PAYLOAD_LEN) {
                            g_imu_parser.error_count++;
                            reset_parser();
                        } else if (g_imu_parser.payload_len == 0) {
                            /* No payload, go directly to checksum */
                            g_imu_parser.state = IMU_STATE_READ_CHECKSUM_1;
                        } else {
                            g_imu_parser.state = IMU_STATE_READ_PAYLOAD;
                        }
                    }
                    break;

                case IMU_STATE_READ_PAYLOAD:
                    g_imu_parser.rx_buffer[g_imu_parser.buffer_pos++] = read_char;

                    /* Check if we have all payload bytes */
                    uint8_t expected_total = YIS320_HEADER_SIZE + g_imu_parser.payload_len;
                    if (g_imu_parser.buffer_pos >= expected_total) {
                        g_imu_parser.state = IMU_STATE_READ_CHECKSUM_1;
                    }
                    break;

                case IMU_STATE_READ_CHECKSUM_1:
                    g_imu_parser.checksum[0] = read_char;
                    g_imu_parser.state = IMU_STATE_READ_CHECKSUM_2;
                    break;

                case IMU_STATE_READ_CHECKSUM_2:
                    g_imu_parser.checksum[1] = read_char;
                    g_imu_parser.state = IMU_STATE_VALIDATE_PARSE;
                    break;

                case IMU_STATE_VALIDATE_PARSE:
                    /* This state is handled below after the while loop */
                    /* We shouldn't get here with a new byte */
                    break;
            }

            /* Check if we need to validate and parse */
            if (g_imu_parser.state == IMU_STATE_VALIDATE_PARSE) {
                uint8_t frame_size = YIS320_HEADER_SIZE + g_imu_parser.payload_len +
                                     YIS320_CHECKSUM_SIZE;

                /* Copy checksum to buffer */
                g_imu_parser.rx_buffer[frame_size - 2] = g_imu_parser.checksum[0];
                g_imu_parser.rx_buffer[frame_size - 1] = g_imu_parser.checksum[1];

#if IMU_ISR_ENABLE_TIMING
                uint64_t parse_start = ClockP_getTimeUsec();
#endif

                if (validate_yis320_checksum(g_imu_parser.rx_buffer, frame_size)) {
                    parse_yis320_frame(g_imu_parser.rx_buffer, frame_size);
                    g_imu_parser.frame_count++;
                    g_isr_stats.frame_count++;
                } else {
                    g_imu_parser.error_count++;
                    g_isr_stats.error_count++;
                }

#if IMU_ISR_ENABLE_TIMING
                uint64_t parse_end = ClockP_getTimeUsec();
                uint32_t parse_us = (uint32_t)(parse_end - parse_start);
                if (parse_us > g_isr_stats.parse_max_us) {
                    g_isr_stats.parse_max_us = parse_us;
                }
                g_isr_stats.parse_total_us += parse_us;
                g_isr_stats.parse_count++;
#endif

                /* Reset for next frame */
                reset_parser();
            }
        }
    }

#if IMU_ISR_ENABLE_TIMING
    uint64_t end_us = ClockP_getTimeUsec();
    uint32_t elapsed_us = (uint32_t)(end_us - start_us);
    if (elapsed_us > g_isr_stats.isr_max_us) {
        g_isr_stats.isr_max_us = elapsed_us;
    }
    g_isr_stats.isr_total_us += elapsed_us;
    g_isr_stats.isr_count++;
#endif
}

/*==============================================================================
 * PUBLIC API
 *============================================================================*/

int imu_uart_isr_init(void)
{
    int32_t status;
    HwiP_Params hwi_params;

    if (g_imu_uart_state.initialized) {
        return 0;  /* Already initialized */
    }

    DebugP_log("[IMU_ISR] Initializing UART interface (Parse-in-ISR mode)...\r\n");

    /* Verify UART is opened */
    if (gUartHandle[CONFIG_UART5] == NULL) {
        DebugP_logError("[IMU_ISR] UART5 handle is NULL!\r\n");
        return -1;
    }

    /* Get UART base address */
    g_uart_base_addr = UART_getBaseAddr(gUartHandle[CONFIG_UART5]);
    if (g_uart_base_addr == 0) {
        DebugP_logError("[IMU_ISR] Invalid UART base address!\r\n");
        return -1;
    }

    DebugP_log("[IMU_ISR] UART5: base=0x%08X\r\n", (unsigned int)g_uart_base_addr);

    /* Reset parser state */
    reset_parser();

    /* Register custom interrupt handler */
    HwiP_Params_init(&hwi_params);
    hwi_params.intNum = gUartParams[CONFIG_UART5].intrNum;
    hwi_params.priority = gUartParams[CONFIG_UART5].intrPriority;
    hwi_params.callback = &imu_uart_isr;
    hwi_params.args = NULL;

    status = HwiP_construct(&g_imu_hwi_object, &hwi_params);
    if (status != SystemP_SUCCESS) {
        DebugP_logError("[IMU_ISR] Failed to register ISR: %d\r\n", status);
        return -1;
    }

    /* Enable RX interrupt */
    UART_intrEnable(g_uart_base_addr, UART_INTR_RHR_CTI);

    g_imu_uart_state.initialized = true;
    g_isr_stats.last_log_time = ClockP_getTimeUsec();

    DebugP_log("[IMU_ISR] UART interface initialized (Parse-in-ISR mode)\r\n");
    DebugP_log("[IMU_ISR] IMU data parsed directly in ISR\r\n");
    DebugP_log("[IMU_ISR] No separate IMU RX task needed\r\n");

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
 * @brief Get current IMU state (atomically)
 *
 * This function reads the IMU state that was updated by ISR.
 * It's safe to call from any task (UDP TX task).
 *
 * @param imu_state Output IMU state structure
 * @return true if new data is available
 */
bool imu_protocol_get_state_isr(imu_state_t* imu_state)
{
    if (imu_state == NULL) {
        return false;
    }

    /* Copy state - struct copy is atomic on ARM for reasonable sizes */
    imu_state->gyroscope[0] = g_imu_atomic_state.gyroscope[0];
    imu_state->gyroscope[1] = g_imu_atomic_state.gyroscope[1];
    imu_state->gyroscope[2] = g_imu_atomic_state.gyroscope[2];

    imu_state->rpy[0] = g_imu_atomic_state.rpy[0];
    imu_state->rpy[1] = g_imu_atomic_state.rpy[1];
    imu_state->rpy[2] = g_imu_atomic_state.rpy[2];

    imu_state->quaternion[0] = g_imu_atomic_state.quaternion[0];
    imu_state->quaternion[1] = g_imu_atomic_state.quaternion[1];
    imu_state->quaternion[2] = g_imu_atomic_state.quaternion[2];
    imu_state->quaternion[3] = g_imu_atomic_state.quaternion[3];

    imu_state->timestamp = g_imu_atomic_state.timestamp;

    bool updated = g_imu_atomic_state.updated;

    return updated;
}

/**
 * @brief Log ISR statistics (call periodically from main loop or task)
 */
void imu_uart_isr_log_stats(void)
{
    uint64_t current_time = ClockP_getTimeUsec();
    if ((current_time - g_isr_stats.last_log_time) >= (IMU_ISR_DEBUG_LOG_INTERVAL * 1000ULL)) {

        uint32_t avg_isr_us = (g_isr_stats.isr_count > 0) ?
                              (g_isr_stats.isr_total_us / g_isr_stats.isr_count) : 0;
        uint32_t avg_parse_us = (g_isr_stats.parse_count > 0) ?
                                (g_isr_stats.parse_total_us / g_isr_stats.parse_count) : 0;

        DebugP_log("[IMU_ISR] Stats: frames=%u, errors=%u, bytes=%u\r\n",
                 g_isr_stats.frame_count, g_isr_stats.error_count, g_imu_parser.byte_count);
        DebugP_log("[IMU_ISR] Timing: ISR(max=%u, avg=%u us), Parse(max=%u, avg=%u us)\r\n",
                 g_isr_stats.isr_max_us, avg_isr_us,
                 g_isr_stats.parse_max_us, avg_parse_us);

        /* Reset statistics */
        memset(&g_isr_stats, 0, sizeof(g_isr_stats));
        g_isr_stats.last_log_time = current_time;
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

    g_imu_uart_state.initialized = false;
    DebugP_log("[IMU_ISR] UART interface deinitialized\r\n");
}

/*==============================================================================
 * STUB CALLBACKS (for syscfg compatibility)
 *============================================================================*/

void imu_uart_read_callback(UART_Handle handle, UART_Transaction *trans)
{
    (void)handle;
    (void)trans;
    /* Not used in ISR mode */
}

void imu_uart_write_callback(UART_Handle handle, UART_Transaction *trans)
{
    (void)handle;
    (void)trans;
    /* Not used in ISR mode */
}
