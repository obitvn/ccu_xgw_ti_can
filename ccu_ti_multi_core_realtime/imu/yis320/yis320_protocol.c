/**
 * @file yis320_protocol.c
 * @brief YIS320 IMU protocol implementation for ccu_ti
 *
 * Adapted from freertos_xgw for TI AM263Px
 *
 * @author Adapted from freertos_xgw by Chu Tien Thinh
 * @date 2025
 */

#include "yis320_protocol.h"
#include "../imu_protocol_handler.h"
#include <string.h>
#include <stddef.h>  /* For NULL */
#include "FreeRTOS.h"  /* For critical sections */
#include "kernel/dpl/DebugP.h"  /* For debug logging */
#include "kernel/dpl/ClockP.h"  /* For timing measurements */

/* Debug logging - uncomment to enable verbose IMU logs */
// #define YIS320_DEBUG_VERBOSE 1

/* Optimization: Limit frames parsed per call to prevent CPU spikes
 * YIS320 outputs ~400Hz = 1 frame per 2.5ms
 * IMU task runs at 500Hz (2ms period)
 * So we should parse at most 1-2 frames per call to avoid processing spikes
 *
 * CIRCULAR BUFFER: No memmove! Uses read/write pointers for O(1) operations */
#define YIS320_MAX_FRAMES_PER_CALL  2  // Reduced from 10 to prevent CPU spikes

/* Enable detailed timing for YIS320 parsing */
#define YIS320_ENABLE_TIMING 1  /* Enable for monitoring */

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
 */
yis320_private_data_t g_yis320_private_data;

/**
 * @brief Debug counters
 */
static uint32_t g_frames_parsed = 0;
static uint32_t g_frames_error = 0;
static uint32_t g_bytes_received = 0;
static uint32_t g_last_log_time = 0;
static uint32_t g_last_parse_time = 0;  /* Timestamp of last successful parse */
static bool     g_gap_logged = false;   /* Flag to avoid spamming gap logs */
static uint32_t g_last_gap_log_time = 0;  /* Timestamp of last gap log */

#if YIS320_ENABLE_TIMING
/* Timing statistics for YIS320 parsing */
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
 */
static const imu_protocol_vtable_t yis320_vtable = {
    .initialize = vtable_initialize,
    .read_data = vtable_read_data,
    .is_connected = vtable_is_connected,
    .deinit = vtable_deinit,
    .process_rx_data = vtable_process_rx_data,
};

/*==============================================================================
 * INTERNAL FUNCTIONS
 *============================================================================*/

/**
 * @brief Find frame header in linear buffer with size limit
 *
 * Optimized for fast scanning without modulo operations.
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
 * @param buffer Frame buffer
 * @param size Frame size
 * @return true if checksum is valid
 */
static bool validate_checksum(const uint8_t* buffer, int size)
{
    if (size < 7) {
        return false;
    }

    // Calculate checksum from TID (byte 2) to end of payload
    uint8_t ck1 = 0, ck2 = 0;
    for (int i = 2; i < size - 2; i++) {
        ck1 += buffer[i];
        ck2 += ck1;
    }

    // Received checksum: CK1 is LSB (second to last byte), CK2 is MSB (last byte)
    uint16_t received_checksum = ((uint16_t)buffer[size - 1] << 8) | buffer[size - 2];
    uint16_t calculated_checksum = ((uint16_t)ck2 << 8) | ck1;

    return (received_checksum == calculated_checksum);
}

/**
 * @brief Find frame header in buffer
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
 *
 * @param data Data payload
 * @param imu_state Output IMU state
 */
static void parse_angular_velocity(const uint8_t* data, imu_state_t* imu_state)
{
    for (int i = 0; i < 3; i++) {
        // Parse signed int32 (little-endian: LSB first)
        int32_t raw = (int32_t)((data[i*4+3] << 24) | (data[i*4+2] << 16) |
                                (data[i*4+1] << 8) | data[i*4]);
        imu_state->gyroscope[i] = (float)raw * YIS320_DATA_FACTOR * YIS320_DEG_TO_RAD;
    }
}

/**
 * @brief Parse Euler angles data
 *
 * Data ID 0x40: Euler angles (pitch, roll, yaw) in degrees
 * Raw data is signed int32, multiplied by factor to get actual value
 *
 * @param data Data payload
 * @param imu_state Output IMU state
 */
static void parse_euler_angles(const uint8_t* data, imu_state_t* imu_state)
{
    for (int i = 0; i < 3; i++) {
        // Parse signed int32 (little-endian: LSB first)
        int32_t raw = (int32_t)((data[i*4+3] << 24) | (data[i*4+2] << 16) |
                                (data[i*4+1] << 8) | data[i*4]);
        imu_state->rpy[i] = (float)raw * YIS320_DATA_FACTOR * YIS320_DEG_TO_RAD;
    }
}

/**
 * @brief Parse quaternion data
 *
 * Data ID 0x41: Quaternion (q0, q1, q2, q3)
 * Raw data is signed int32, multiplied by factor to get actual value
 *
 * @param data Data payload
 * @param imu_state Output IMU state
 */
static void parse_quaternion(const uint8_t* data, imu_state_t* imu_state)
{
    for (int i = 0; i < 4; i++) {
        // Parse signed int32 (little-endian: LSB first)
        int32_t raw = (int32_t)((data[i*4+3] << 24) | (data[i*4+2] << 16) |
                                (data[i*4+1] << 8) | data[i*4]);
        imu_state->quaternion[i] = (float)raw * YIS320_DATA_FACTOR;
    }
}

/**
 * @brief Parse complete YIS320 frame
 *
 * @param buffer Frame buffer
 * @param size Frame size
 * @param imu_state Output IMU state
 * @return true if frame was parsed successfully
 */
static bool parse_imu_frame(const uint8_t* buffer, int size, imu_state_t* imu_state)
{
    // Check header
    if (buffer[0] != YIS320_HEADER_BYTE_1 || buffer[1] != YIS320_HEADER_BYTE_2) {
        return false;
    }

    // Extract TID and length
    uint16_t tid = buffer[2] | (buffer[3] << 8);
    (void)tid;  /* TID is available but not used currently */
    uint8_t len = buffer[4];

    // Validate checksum
    if (!validate_checksum(buffer, size)) {
        static uint32_t cksum_err_count = 0;
        if (cksum_err_count < 10) {
            // Dump first 16 bytes for debugging
            // DebugP_log("[YIS320] CRC error #%u: size=%d, tid=%u, len=%u\r\n",
            //          cksum_err_count, size, tid, len);
            // DebugP_log("[YIS320] Data: ");
            // for (int i = 0; i < 16 && i < size; i++) {
            //     DebugP_log("%02X ", buffer[i]);
            // }
            // DebugP_log("\r\n");
            cksum_err_count++;
        }
        return false;
    }

    // Parse data items
    int offset = 5;
    while (offset < 5 + len) {
        // Need at least data_id (1B) + data_len (1B)
        if (offset + 2 > size) {
            break;
        }

        uint8_t data_id = buffer[offset];
        uint8_t data_len = buffer[offset + 1];

        // Check we have full data item
        if (offset + 2 + data_len > size) {
            break;
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

            // Other data types can be added as needed
            default:
                // Unknown data type, skip
                break;
        }

        offset += 2 + data_len;
    }

    return true;
}

/*==============================================================================
 * PUBLIC FUNCTIONS
 *============================================================================*/

int yis320_protocol_handler_init(imu_protocol_handler_t* handler)
{
    if (handler == NULL) {
        return -1;
    }

    // Use static allocation - no malloc needed
    // Initialize static private data
    memset(&g_yis320_private_data, 0, sizeof(yis320_private_data_t));
    g_yis320_private_data.is_connected = false;
    g_yis320_private_data.baud_rate = YIS320_DEFAULT_BAUD_RATE;
    g_yis320_private_data.uart_port = 5;  // UART5 for IMU
    g_yis320_private_data.read_pos = 0;

    // Set vtable
    handler->vtable = &yis320_vtable;
    handler->imu_type = IMU_TYPE_YIS320;
    handler->private_data = &g_yis320_private_data;  // Point to static data

    return 0;
}

static int vtable_initialize(imu_protocol_handler_t* handler)
{
    if (handler == NULL || handler->private_data == NULL) {
        return -1;
    }

    yis320_private_data_t* priv = (yis320_private_data_t*)handler->private_data;

    // Configure UART
    if (yis320_uart_configure(priv->uart_port, priv->baud_rate) != 0) {
        return -1;
    }

    // TODO: Send configuration commands to YIS320 if needed
    // For now, YIS320 outputs data automatically on power-on

    priv->is_connected = true;
    priv->read_pos = 0;

    return 0;
}

static bool vtable_read_data(imu_protocol_handler_t* handler, imu_state_t* imu_state)
{
    // For YIS320, data is processed incrementally via process_rx_data
    // This function is a no-op but kept for API compatibility
    (void)handler;
    (void)imu_state;
    return false;
}

static bool vtable_is_connected(imu_protocol_handler_t* handler)
{
    if (handler == NULL || handler->private_data == NULL) {
        return false;
    }

    yis320_private_data_t* priv = (yis320_private_data_t*)handler->private_data;
    return priv->is_connected;
}

static void vtable_deinit(imu_protocol_handler_t* handler)
{
    if (handler == NULL || handler->private_data == NULL) {
        return;
    }

    yis320_private_data_t* priv = (yis320_private_data_t*)handler->private_data;
    priv->is_connected = false;

    // No need to free - using static allocation
    handler->private_data = NULL;

    // Note: UART deinitialization should be handled by platform-specific code
}

static void vtable_process_rx_data(imu_protocol_handler_t* handler, const uint8_t* data, uint16_t length)
{
    if (handler == NULL || handler->private_data == NULL || data == NULL || length == 0) {
        return;
    }

#if YIS320_ENABLE_TIMING
    uint64_t start_us = ClockP_getTimeUsec();
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
    uint16_t copy_len = length;
    if (priv->read_pos + copy_len > YIS320_BUFFER_SIZE) {
        copy_len = YIS320_BUFFER_SIZE - priv->read_pos;
    }
    memcpy(priv->rx_buffer + priv->read_pos, data, copy_len);
    priv->read_pos += copy_len;

    if (length > copy_len) {
        /* Buffer overflow - drop oldest data */
        uint16_t overflow = length - copy_len;
        g_frames_error++;
    }
    /* ========== END APPEND ========== */

    /* ========== LINEAR BUFFER: Parse all frames, single memmove at end ========== */
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

        if (len > 200 || frame_size > (int)YIS320_BUFFER_SIZE) {
            consumed++;  /* Skip invalid byte */
            g_frames_error++;
            continue;
        }

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
            DebugP_log("[YIS320] Frame parsed OK: size=%d, gyro=(%.3f,%.3f,%.3f)\r\n",
                     frame_size, temp_state.gyroscope[0], temp_state.gyroscope[1], temp_state.gyroscope[2]);
#endif

            temp_state.timestamp = g_last_parse_time;
            imu_protocol_update_state(&temp_state);

            consumed += frame_size;  /* Just advance pointer - NO memmove! */
            frames_parsed_this_call++;
        } else {
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
    uint64_t end_us = ClockP_getTimeUsec();
    uint32_t elapsed_us = (uint32_t)(end_us - start_us);
    if (elapsed_us > g_yis320_timing.parse_max_us) {
        g_yis320_timing.parse_max_us = elapsed_us;
    }
    g_yis320_timing.parse_total_us += elapsed_us;
    g_yis320_timing.parse_count++;

    if ((current_time - g_yis320_timing.last_log_time) >= 5000) {
        uint32_t avg_parse = (g_yis320_timing.parse_count > 0) ?
                            (g_yis320_timing.parse_total_us / g_yis320_timing.parse_count) : 0;
        DebugP_log("[YIS320] Timing: max=%u us, avg=%u us, count=%u\r\n",
                 g_yis320_timing.parse_max_us, avg_parse, g_yis320_timing.parse_count);
        g_yis320_timing.parse_max_us = 0;
        g_yis320_timing.parse_total_us = 0;
        g_yis320_timing.parse_count = 0;
        g_yis320_timing.last_log_time = current_time;
    }
#endif

    if ((current_time - g_last_log_time) >= 5000) {
        g_last_log_time = current_time;
    }
}
