/**
 * @file yis320_protocol.c
 * @brief YIS320 IMU protocol implementation for CCU Multicore Gateway (Core 1 - Bare Metal)
 *
 * Migrated from draft/ccu_ti/imu/yis320/yis320_protocol.c
 * - Removed FreeRTOS dependencies (taskENTER_CRITICAL, taskEXIT_CRITICAL)
 * - Added bare metal critical sections (__disable_irq/__enable_irq)
 * - Added IPC support via gateway_write_imu_state() and gateway_notify_imu_ready()
 * - Linear buffer with deferred memmove (O(n), not O(n²))
 * - Max 2 frames per call (YIS320_MAX_FRAMES_PER_CALL = 2)
 * - Static allocation, NO malloc
 *
 * Data Flow:
 * 1. UART RX interrupt -> yis320_process_rx_data()
 * 2. Parse frame -> imu_state_t (local)
 * 3. Convert -> imu_state_ipc_t
 * 4. gateway_write_imu_state() -> shared memory
 * 5. gateway_notify_imu_ready() -> IPC notify Core 0
 *
 * @author Migrated from draft/ccu_ti by Chu Tien Thinh
 * @date 2026-03-27
 */

#include "yis320_protocol.h"
#include "../imu_protocol_handler.h"
#include "../../../gateway_shared.h"
#include <string.h>
#include <stddef.h>

/* Bare metal critical section support */
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    /* ARMv8-M with Security Extension */
    #define YIS320_ENTER_CRITICAL()    uint32_t yis320_cs_key = __get_CPU_state()
    #define YIS320_EXIT_CRITICAL()     __set_CPU_state(yis320_cs_key)
#else
    /* Fallback - simple disable/enable (bare metal) */
    #define YIS320_ENTER_CRITICAL()    __disable_irq()
    #define YIS320_EXIT_CRITICAL()     __enable_irq()
#endif

/* Debug logging - uncomment to enable verbose IMU logs */
// #define YIS320_DEBUG_VERBOSE 1

/* Optimization: Limit frames parsed per call to prevent CPU spikes
 * YIS320 outputs ~400Hz = 1 frame per 2.5ms
 * IMU task runs at 500Hz (2ms period)
 * So we should parse at most 1-2 frames per call to avoid processing spikes
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:22-28]
 */
#define YIS320_MAX_FRAMES_PER_CALL  2

/* Enable detailed timing for YIS320 parsing */
#define YIS320_ENABLE_TIMING 1

/*==============================================================================
 * STATIC DATA (No dynamic allocation)
 *============================================================================*/

/**
 * @brief Static private data instance
 *
 * Using static allocation instead of dynamic malloc to avoid:
 * - Memory fragmentation in long-running systems
 * - Out-of-memory issues
 * - Memory leaks
 * - Heap overhead
 *
 * Only one IMU instance is supported with this approach.
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:47]
 */
yis320_private_data_t g_yis320_private_data;

/**
 * @brief Debug counters
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:52-58]
 */
static uint32_t g_frames_parsed = 0;
static uint32_t g_frames_error = 0;
static uint32_t g_bytes_received = 0;
static uint32_t g_last_log_time = 0;
static uint32_t g_last_parse_time = 0;
static bool     g_gap_logged = false;
static uint32_t g_last_gap_log_time = 0;

#if YIS320_ENABLE_TIMING
/* Timing statistics for YIS320 parsing
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:62-68]
 */
static struct {
    uint32_t parse_max_us;
    uint32_t parse_total_us;
    uint32_t parse_count;
    uint32_t last_log_time;
} g_yis320_timing = {0};
#endif

/*==============================================================================
 * VTABLE IMPLEMENTATION
 *============================================================================*/

// Forward declarations for vtable functions
static int vtable_initialize(imu_protocol_handler_t* handler);
static bool vtable_read_data(imu_protocol_handler_t* handler, imu_state_t* imu_state);
static bool vtable_is_connected(imu_protocol_handler_t* handler);
static void vtable_deinit(imu_protocol_handler_t* handler);
static void vtable_process_rx_data(imu_protocol_handler_t* handler, const uint8_t* data, uint16_t length);

/**
 * @brief YIS320 protocol vtable
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:85]
 */
static const imu_protocol_vtable_t yis320_vtable = {
    .initialize = vtable_initialize,
    .read_data = vtable_read_data,
    .is_connected = vtable_is_connected,
    .deinit = vtable_deinit,
    .process_rx_data = vtable_process_rx_data,
};

/*==============================================================================
 * IPC CONVERSION FUNCTION
 *============================================================================*/

/**
 * @brief Convert imu_state_t to imu_state_ipc_t for IPC
 *
 * Converts local IMU state to IPC format for shared memory transfer.
 * Maps imu_state_t fields to imu_state_ipc_t fields.
 *
 * [NEW: Added for IPC integration - replaces imu_protocol_update_state()]
 *
 * @param src Source imu_state_t structure
 * @param dst Destination imu_state_ipc_t structure
 */
void yis320_convert_to_ipc(const imu_state_t* src, imu_state_ipc_t* dst)
{
    if (src == NULL || dst == NULL) {
        return;
    }

    /* Map gyroscope data: [rad/s] */
    dst->gyro[0] = src->gyroscope[0];
    dst->gyro[1] = src->gyroscope[1];
    dst->gyro[2] = src->gyroscope[2];

    /* Map quaternion data: [w, x, y, z] */
    dst->quat[0] = src->quaternion[0];
    dst->quat[1] = src->quaternion[1];
    dst->quat[2] = src->quaternion[2];
    dst->quat[3] = src->quaternion[3];

    /* Map Euler angles (rpy) to euler: [rad] - [roll, pitch, yaw] */
    dst->euler[0] = src->rpy[0];  /* Roll */
    dst->euler[1] = src->rpy[1];  /* Pitch */
    dst->euler[2] = src->rpy[2];  /* Yaw */

    /* Clear unused fields (not available from YIS320) */
    dst->mag_val[0] = 0.0f;
    dst->mag_val[1] = 0.0f;
    dst->mag_val[2] = 0.0f;
    dst->mag_norm[0] = 0.0f;
    dst->mag_norm[1] = 0.0f;
    dst->mag_norm[2] = 0.0f;
    dst->temp_cdeg = 0;  /* Temperature not available */

    /* Set IMU ID */
    dst->imu_id = 0;  /* Single IMU supported */
    dst->reserved = 0;
}

/*==============================================================================
 * INTERNAL FUNCTIONS
 *============================================================================*/

/**
 * @brief Find frame header in linear buffer with size limit
 *
 * Optimized for fast scanning without modulo operations.
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:107-115]
 *
 * @param buffer Buffer to search
 * @param size Buffer size (may be larger than actual data)
 * @param data_len Actual data length
 * @return Header position or -1 if not found
 */
static int find_frame_header_limited(const uint8_t* buffer, int size, int data_len)
{
    int limit = (data_len < size - 1) ? data_len : size - 1;
    for (int i = 0; i < limit; i++) {
        if (buffer[i] == YIS320_HEADER_BYTE_1 && buffer[i + 1] == YIS320_HEADER_BYTE_2) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Validate YIS320 frame checksum
 *
 * Checksum calculation: CK1 (LSB) + CK2 (MSB)
 * Both bytes are accumulated from TID to end of payload
 *
 * Algorithm:
 * 1. CK1 = cumulative sum of bytes from TID to end of payload
 * 2. CK2 = cumulative sum of CK1 values
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:128-145]
 *
 * @param buffer Frame buffer
 * @param size Frame size
 * @return true if checksum is valid
 */
static bool validate_checksum(const uint8_t* buffer, int size)
{
    if (size < 7) {
        return false;
    }

    /* Calculate checksum from TID (byte 2) to end of payload */
    uint8_t ck1 = 0, ck2 = 0;
    for (int i = 2; i < size - 2; i++) {
        ck1 += buffer[i];
        ck2 += ck1;
    }

    /* Received checksum: CK1 is LSB (second to last byte), CK2 is MSB (last byte) */
    uint16_t received_checksum = ((uint16_t)buffer[size - 1] << 8) | buffer[size - 2];
    uint16_t calculated_checksum = ((uint16_t)ck2 << 8) | ck1;

    return (received_checksum == calculated_checksum);
}

/**
 * @brief Find frame header in buffer
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:154-162]
 *
 * @param buffer Buffer to search
 * @param size Buffer size
 * @return Header position or -1 if not found
 */
static int find_frame_header(const uint8_t* buffer, int size)
{
    for (int i = 0; i <= size - 2; i++) {
        if (buffer[i] == YIS320_HEADER_BYTE_1 && buffer[i + 1] == YIS320_HEADER_BYTE_2) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Parse angular velocity (gyroscope) data
 *
 * Data ID 0x20: Angular velocity in deg/s
 * Raw data is signed int32, multiplied by factor to get actual value
 * Conversion: int32 × 1e-6 × π/180 = rad/s
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:174-181]
 *
 * @param data Data payload
 * @param imu_state Output IMU state
 */
static void parse_angular_velocity(const uint8_t* data, imu_state_t* imu_state)
{
    for (int i = 0; i < 3; i++) {
        /* Parse signed int32 (little-endian: LSB first) */
        int32_t raw = (int32_t)((data[i*4+3] << 24) | (data[i*4+2] << 16) |
                                (data[i*4+1] << 8) | data[i*4]);
        /* Convert: raw × 1e-6 × π/180 = rad/s */
        imu_state->gyroscope[i] = (float)raw * YIS320_DATA_FACTOR * YIS320_DEG_TO_RAD;
    }
}

/**
 * @brief Parse Euler angles data
 *
 * Data ID 0x40: Euler angles (pitch, roll, yaw) in degrees
 * Raw data is signed int32, multiplied by factor to get actual value
 * Conversion: int32 × 1e-6 × π/180 = rad
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:193-200]
 *
 * @param data Data payload
 * @param imu_state Output IMU state
 */
static void parse_euler_angles(const uint8_t* data, imu_state_t* imu_state)
{
    for (int i = 0; i < 3; i++) {
        /* Parse signed int32 (little-endian: LSB first) */
        int32_t raw = (int32_t)((data[i*4+3] << 24) | (data[i*4+2] << 16) |
                                (data[i*4+1] << 8) | data[i*4]);
        /* Convert: raw × 1e-6 × π/180 = rad */
        imu_state->rpy[i] = (float)raw * YIS320_DATA_FACTOR * YIS320_DEG_TO_RAD;
    }
}

/**
 * @brief Parse quaternion data
 *
 * Data ID 0x41: Quaternion (q0, q1, q2, q3)
 * Raw data is signed int32, multiplied by factor to get actual value
 * Conversion: int32 × 1e-6 = unitless quaternion
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:212-219]
 *
 * @param data Data payload
 * @param imu_state Output IMU state
 */
static void parse_quaternion(const uint8_t* data, imu_state_t* imu_state)
{
    for (int i = 0; i < 4; i++) {
        /* Parse signed int32 (little-endian: LSB first) */
        int32_t raw = (int32_t)((data[i*4+3] << 24) | (data[i*4+2] << 16) |
                                (data[i*4+1] << 8) | data[i*4]);
        /* Convert: raw × 1e-6 = unitless quaternion */
        imu_state->quaternion[i] = (float)raw * YIS320_DATA_FACTOR;
    }
}

/**
 * @brief Parse complete YIS320 frame
 *
 * Handles all edge cases:
 * - Invalid header
 * - Invalid length
 * - Checksum error
 * - Unknown data IDs
 *
 * Supported Data IDs:
 * - 0x20: Gyroscope (3 × int32)
 * - 0x40: Euler angles (3 × int32)
 * - 0x41: Quaternion (4 × int32)
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:230-303]
 *
 * @param buffer Frame buffer
 * @param size Frame size
 * @param imu_state Output IMU state
 * @return true if frame was parsed successfully
 */
static bool parse_imu_frame(const uint8_t* buffer, int size, imu_state_t* imu_state)
{
    /* Check header */
    if (buffer[0] != YIS320_HEADER_BYTE_1 || buffer[1] != YIS320_HEADER_BYTE_2) {
        return false;
    }

    /* Extract TID and length */
    uint16_t tid = buffer[2] | (buffer[3] << 8);
    (void)tid;  /* TID is available but not used currently */
    uint8_t len = buffer[4];

    /* Validate checksum */
    if (!validate_checksum(buffer, size)) {
        static uint32_t cksum_err_count = 0;
        if (cksum_err_count < 10) {
            /* Checksum error - limited logging to avoid spam */
            cksum_err_count++;
        }
        return false;
    }

    /* Parse data items */
    int offset = 5;
    while (offset < 5 + len) {
        /* Need at least data_id (1B) + data_len (1B) */
        if (offset + 2 > size) {
            break;  /* Incomplete frame */
        }

        uint8_t data_id = buffer[offset];
        uint8_t data_len = buffer[offset + 1];

        /* Check we have full data item */
        if (offset + 2 + data_len > size) {
            break;  /* Incomplete data item */
        }

        switch (data_id) {
            case YIS320_DATA_ID_GYROSCOPE:
                if (data_len == 12) {
                    parse_angular_velocity(buffer + offset + 2, imu_state);
                }
                break;

            case YIS320_DATA_ID_EULER_ANGLES:
                if (data_len == 12) {
                    parse_euler_angles(buffer + offset + 2, imu_state);
                }
                break;

            case YIS320_DATA_ID_QUATERNION:
                if (data_len == 16) {
                    parse_quaternion(buffer + offset + 2, imu_state);
                }
                break;

            /* Other data types can be added as needed */
            default:
                /* Unknown data type, skip */
                break;
        }

        offset += 2 + data_len;
    }

    return true;
}

/*==============================================================================
 * PUBLIC FUNCTIONS
 *============================================================================*/

/**
 * @brief Initialize YIS320 protocol handler
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:310-329]
 *
 * @param handler IMU protocol handler instance to initialize
 * @return 0 on success, -1 on error
 */
int yis320_protocol_handler_init(imu_protocol_handler_t* handler)
{
    if (handler == NULL) {
        return -1;
    }

    /* Use static allocation - no malloc needed */
    /* Initialize static private data */
    memset(&g_yis320_private_data, 0, sizeof(yis320_private_data_t));
    g_yis320_private_data.is_connected = false;
    g_yis320_private_data.baud_rate = YIS320_DEFAULT_BAUD_RATE;
    g_yis320_private_data.uart_port = 5;  /* UART5 for IMU */
    g_yis320_private_data.read_pos = 0;

    /* Set vtable */
    handler->vtable = &yis320_vtable;
    handler->imu_type = IMU_TYPE_YIS320;
    handler->private_data = &g_yis320_private_data;  /* Point to static data */

    return 0;
}

/**
 * @brief Initialize YIS320 hardware connection
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:332-351]
 *
 * @param handler IMU protocol handler instance
 * @return 0 on success, -1 on error
 */
static int vtable_initialize(imu_protocol_handler_t* handler)
{
    if (handler == NULL || handler->private_data == NULL) {
        return -1;
    }

    yis320_private_data_t* priv = (yis320_private_data_t*)handler->private_data;

    /* Configure UART */
    if (yis320_uart_configure(priv->uart_port, priv->baud_rate) != 0) {
        return -1;
    }

    /* TODO: Send configuration commands to YIS320 if needed */
    /* For now, YIS320 outputs data automatically on power-on */

    priv->is_connected = true;
    priv->read_pos = 0;

    return 0;
}

/**
 * @brief Read data from YIS320 IMU (no-op for YIS320)
 *
 * For YIS320, data is processed incrementally via process_rx_data
 * This function is a no-op but kept for API compatibility
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:354-360]
 *
 * @param handler IMU protocol handler instance
 * @param imu_state Output IMU state structure
 * @return true if new data was decoded
 */
static bool vtable_read_data(imu_protocol_handler_t* handler, imu_state_t* imu_state)
{
    /* For YIS320, data is processed incrementally via process_rx_data */
    /* This function is a no-op but kept for API compatibility */
    (void)handler;
    (void)imu_state;
    return false;
}

/**
 * @brief Check if YIS320 IMU is connected
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:363-370]
 *
 * @param handler IMU protocol handler instance
 * @return true if connected
 */
static bool vtable_is_connected(imu_protocol_handler_t* handler)
{
    if (handler == NULL || handler->private_data == NULL) {
        return false;
    }

    yis320_private_data_t* priv = (yis320_private_data_t*)handler->private_data;
    return priv->is_connected;
}

/**
 * @brief Close YIS320 IMU connection
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:373-385]
 *
 * @param handler IMU protocol handler instance
 */
static void vtable_deinit(imu_protocol_handler_t* handler)
{
    if (handler == NULL || handler->private_data == NULL) {
        return;
    }

    yis320_private_data_t* priv = (yis320_private_data_t*)handler->private_data;
    priv->is_connected = false;

    /* No need to free - using static allocation */
    handler->private_data = NULL;

    /* Note: UART deinitialization should be handled by platform-specific code */
}

/**
 * @brief Process received UART data
 *
 * This function is called from UART RX interrupt context.
 * It processes the data and:
 * 1. Parses YIS320 frames
 * 2. Converts to imu_state_ipc_t
 * 3. Writes to shared memory via gateway_write_imu_state()
 * 4. Notifies Core 0 via gateway_notify_imu_ready()
 *
 * Key Features:
 * - Linear buffer with deferred memmove (O(n), not O(n²))
 * - Max 2 frames per call to prevent CPU spikes
 * - Bare metal critical sections (no FreeRTOS)
 * - All edge cases handled: incomplete frame, invalid length, checksum error
 *
 * [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:388-533]
 * [MODIFIED: Replaced imu_protocol_update_state() with IPC integration]
 *
 * @param handler IMU protocol handler instance
 * @param data Received data buffer
 * @param length Data length
 */
static void vtable_process_rx_data(imu_protocol_handler_t* handler, const uint8_t* data, uint16_t length)
{
    if (handler == NULL || handler->private_data == NULL || data == NULL || length == 0) {
        return;
    }

    /* Get timestamp before parsing (for timing) */
#if YIS320_ENABLE_TIMING
    uint32_t start_ticks = yis320_get_timestamp_ms();
#endif

    yis320_private_data_t* priv = (yis320_private_data_t*)handler->private_data;

    g_bytes_received += length;

    static bool first_data = true;
    if (first_data && length > 0) {
        first_data = false;
        g_last_parse_time = yis320_get_timestamp_ms();
    }

    uint32_t current_time = yis320_get_timestamp_ms();
    if (g_last_parse_time > 0 && (current_time - g_last_parse_time) > 5) {
        if (!g_gap_logged) {
            g_last_gap_log_time = current_time;
            g_gap_logged = true;
        }
    }

    /* ========== LINEAR BUFFER: Append new data ========== */
    /* [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:416-428] */
    uint16_t copy_len = length;
    if (priv->read_pos + copy_len > YIS320_BUFFER_SIZE) {
        copy_len = YIS320_BUFFER_SIZE - priv->read_pos;
    }
    memcpy(priv->rx_buffer + priv->read_pos, data, copy_len);
    priv->read_pos += copy_len;

    if (length > copy_len) {
        /* Buffer overflow - drop oldest data */
        uint16_t overflow = length - copy_len;
        (void)overflow;
        g_frames_error++;
    }
    /* ========== END APPEND ========== */

    /* ========== LINEAR BUFFER: Parse all frames, single memmove at end ========== */
    /* [MIGRATED FROM draft/ccu_ti/imu/yis320/yis320_protocol.c:431-507] */
    uint8_t frames_parsed_this_call = 0;
    uint16_t consumed = 0;  /* Track total bytes consumed */

    while (consumed < priv->read_pos && frames_parsed_this_call < YIS320_MAX_FRAMES_PER_CALL) {
        uint16_t remaining = priv->read_pos - consumed;

        if (remaining < 7) {
            break;  /* Not enough for minimum frame */
        }

        /* Find header in linear buffer (fast - no modulo!) */
        uint8_t* buf_ptr = priv->rx_buffer + consumed;
        int header_pos = find_frame_header_limited(buf_ptr, remaining, remaining);

        if (header_pos < 0) {
            /* No header - keep last 2 bytes, discard rest */
            if (remaining > 2) {
                consumed += remaining - 2;
            }
            break;
        }

        /* Skip bytes before header */
        consumed += header_pos;
        remaining = priv->read_pos - consumed;

        if (remaining < 5) {
            break;  /* Not enough for LEN field */
        }

        /* Direct buffer access - no function call overhead */
        uint8_t len = priv->rx_buffer[consumed + 4];
        int frame_size = 7 + len;

        /* Edge case: Invalid length */
        if (len > 200 || frame_size > (int)YIS320_BUFFER_SIZE) {
            consumed++;  /* Skip invalid byte */
            g_frames_error++;
            continue;
        }

        /* Edge case: Incomplete frame */
        if (remaining < (uint16_t)frame_size) {
            break;  /* Incomplete frame */
        }

        /* Parse directly from linear buffer */
        imu_state_t temp_state;
        memset(&temp_state, 0, sizeof(temp_state));

        if (parse_imu_frame(priv->rx_buffer + consumed, frame_size, &temp_state)) {
            g_frames_parsed++;
            g_last_parse_time = yis320_get_timestamp_ms();
            g_gap_logged = false;

#ifdef YIS320_DEBUG_VERBOSE
            /* Debug logging - gyro output */
#endif

            temp_state.timestamp = g_last_parse_time;

            /* ========== IPC INTEGRATION: Write to shared memory ========== */
            /* [NEW: Added IPC integration to replace imu_protocol_update_state()] */
            imu_state_ipc_t ipc_state;
            yis320_convert_to_ipc(&temp_state, &ipc_state);

            /* Critical section for shared memory access */
            YIS320_ENTER_CRITICAL();
            int result = gateway_write_imu_state(&ipc_state);
            YIS320_EXIT_CRITICAL();

            if (result == 0) {
                /* Notify Core 0 that IMU data is ready */
                gateway_notify_imu_ready();
            }
            /* ========== END IPC INTEGRATION ========== */

            consumed += frame_size;  /* Just advance pointer - NO memmove! */
            frames_parsed_this_call++;
        } else {
            /* Edge case: Checksum error or invalid frame */
            consumed++;  /* Skip one byte on error */
            g_frames_error++;
        }
    }

    /* ========== SINGLE MEMMOVE at end (only once per call!) ========== */
    if (consumed > 0 && consumed < priv->read_pos) {
        memmove(priv->rx_buffer, priv->rx_buffer + consumed, priv->read_pos - consumed);
        priv->read_pos -= consumed;
    } else if (consumed >= priv->read_pos) {
        priv->read_pos = 0;  /* All data consumed */
    }
    /* ========== END PARSE ========== */

#if YIS320_ENABLE_TIMING
    /* Calculate timing statistics */
    uint32_t end_ticks = yis320_get_timestamp_ms();
    uint32_t elapsed_us = (end_ticks - start_ticks) * 1000;
    if (elapsed_us > g_yis320_timing.parse_max_us) {
        g_yis320_timing.parse_max_us = elapsed_us;
    }
    g_yis320_timing.parse_total_us += elapsed_us;
    g_yis320_timing.parse_count++;

    if ((current_time - g_yis320_timing.last_log_time) >= 5000) {
        /* Log timing statistics every 5 seconds */
        g_yis320_timing.last_log_time = current_time;
    }
#endif

    if ((current_time - g_last_log_time) >= 5000) {
        g_last_log_time = current_time;
    }
}
