/**
 * @file xgw_protocol.c
 * @brief xGW-Linux UDP Protocol v2.0 implementation
 *
 * Protocol helper functions for xGW communication
 * Multi-core compatible: Core0 (RTOS) + Core3 (bare metal)
 *
 * [MIGRATED FROM draft/ccu_ti/xgw_protocol.c 2026-03-27]
 * Key changes for bare metal compatibility:
 * - Removed all dynamic memory allocation (malloc/free)
 * - Conditional logging based on feature flags
 * - Stack-only CRC calculation (max 1024 bytes)
 * - Enhanced validation with compile-time feature flags
 */

#include "xgw_protocol.h"
#include "crc32.h"
#include <string.h>

/*==============================================================================
 * CONDITIONAL LOGGING SUPPORT
 *============================================================================*/

/* Conditional logging wrapper for bare metal compatibility */
#if XGW_PROTOCOL_ENABLE_LOG
    /* RTOS/Core0: Use DebugP logging */
    #include "kernel/dpl/DebugP.h"
    #define XGW_LOG(fmt, ...) DebugP_log(fmt, ##__VA_ARGS__)
    #define XGW_LOG_ENABLED 1
#else
    /* Bare metal/Core3: Disable all logging */
    #define XGW_LOG(fmt, ...) ((void)0)
    #define XGW_LOG_ENABLED 0
#endif

/*==============================================================================
 * INTERNAL CONSTANTS
 *============================================================================*/

/* CRC32-IEEE (CRC-32/ISO-HDLC) Polynomial
 * Config: Poly=0x04C11DB7, Init=0xFFFFFFFF, FinalXOR=0xFFFFFFFF, RefIn=True, RefOut=True
 * Standard used in: Ethernet, ZIP, GZIP, PNG, SATA, HDLC, etc.
 * [MIGRATED FROM draft/ccu_ti/xgw_protocol.c:20]
 */
#define CRC32_POLYNOMIAL  0x04c11db7

/* Stack buffer size for CRC calculation (configurable via feature flag) */
#define CRC_BUFFER_SIZE_WORDS XGW_PROTOCOL_CRC_BUFFER_SIZE

/*==============================================================================
 * PUBLIC FUNCTIONS - HEADER MANAGEMENT
 *============================================================================*/

/**
 * @brief Initialize protocol header with default values
 * @param header Pointer to header structure
 * @param msg_type Message type
 * @param count Number of elements in payload
 * @param payload_len Payload length in bytes
 * @param sequence Packet sequence number
 * [MIGRATED FROM draft/ccu_ti/xgw_protocol.c:26]
 */
void xgw_header_init(xgw_header_t* header, uint8_t msg_type, uint8_t count,
                     uint32_t payload_len, uint32_t sequence)
{
#if XGW_PROTOCOL_ENABLE_VALIDATION
    if (header == NULL) {
        XGW_LOG("[XGW Protocol] ERROR: NULL header pointer\r\n");
        return;
    }
#endif

    header->magic = XGW_PROTOCOL_MAGIC;
    header->header_meta = 0;
    header->version = XGW_PROTOCOL_VERSION;
    header->msg_type = msg_type;
    header->count = count;
    header->reserved = 0;
    header->sequence = sequence;
    header->payload_len = payload_len;
    header->timestamp_ns = 0;  /* Will be set by caller */
    header->crc32 = 0;          /* Will be calculated */
    header->reserved_pad = 0;
}

/**
 * @brief Calculate CRC32 for packet
 * @param header Packet header
 * @param payload Payload data
 * @param payload_len Payload length
 * @return CRC32 value
 *
 * Implementation notes for bare metal compatibility:
 * - Uses stack allocation only (no malloc)
 * - Maximum packet size supported: CRC_BUFFER_SIZE_WORDS * 4 bytes
 * - For xGW protocol: max packet = 32 (header) + 23*24 (motor cmds) = 584 bytes
 * - This implementation allocates CRC_BUFFER_SIZE_WORDS (default: 256 words = 1024 bytes)
 * - Returns 0 on error or if packet is too large
 * [MIGRATED FROM draft/ccu_ti/xgw_protocol.c:46]
 */
uint32_t xgw_crc32_calculate(const xgw_header_t* header, const void* payload, uint32_t payload_len)
{
#if XGW_PROTOCOL_ENABLE_VALIDATION
    if (header == NULL) {
        XGW_LOG("[XGW Protocol] ERROR: NULL header in CRC calculation\r\n");
        return 0;
    }
#endif

    /* Calculate total size in bytes */
    uint32_t total_bytes = sizeof(xgw_header_t) + payload_len;
    uint32_t total_words = (total_bytes + 3) / 4;  /* Round up to word boundary */

    /* Validate packet size against buffer capacity */
#if XGW_PROTOCOL_ENABLE_VALIDATION
    if (total_words > CRC_BUFFER_SIZE_WORDS) {
        XGW_LOG("[XGW Protocol] ERROR: Packet too large for CRC calculation\r\n");
        XGW_LOG("  Total bytes: %u, Buffer capacity: %u bytes\r\n",
                total_bytes, CRC_BUFFER_SIZE_WORDS * 4);
        return 0;
    }
#endif

    /* Stack buffer for CRC calculation (no malloc) */
    uint32_t data_buffer[CRC_BUFFER_SIZE_WORDS];

    /* Copy header and set CRC to 0 for calculation */
    memcpy(data_buffer, header, sizeof(xgw_header_t));
    ((xgw_header_t*)data_buffer)->crc32 = 0;

    /* Copy payload if present */
    if (payload != NULL && payload_len > 0) {
        memcpy((uint8_t*)data_buffer + sizeof(xgw_header_t), payload, payload_len);
    }

    /* Calculate CRC - pass byte count, crc32_core handles 0x00 padding */
    return crc32_core((const uint8_t*)data_buffer, total_bytes);
}

/**
 * @brief Validate packet CRC32
 * @param header Packet header
 * @param payload Payload data
 * @return true if CRC is valid, false otherwise
 * [MIGRATED FROM draft/ccu_ti/xgw_protocol.c:91]
 */
bool xgw_crc32_validate(const xgw_header_t* header, const void* payload)
{
#if XGW_PROTOCOL_ENABLE_VALIDATION
    if (header == NULL) {
        XGW_LOG("[XGW Protocol] ERROR: NULL header in CRC validation\r\n");
        return false;
    }
#endif

    uint32_t calc_crc = xgw_crc32_calculate(header, payload, header->payload_len);
    bool is_valid = (calc_crc == header->crc32);

#if XGW_PROTOCOL_ENABLE_VALIDATION && XGW_PROTOCOL_ENABLE_LOG
    if (!is_valid) {
        XGW_LOG("[XGW Protocol] CRC validation failed: expected=0x%08X, calculated=0x%08X\r\n",
                header->crc32, calc_crc);
    }
#endif

    return is_valid;
}

/**
 * @brief Get message type string (for debugging)
 * @param msg_type Message type
 * @return String representation
 * [MIGRATED FROM draft/ccu_ti/xgw_protocol.c:101]
 */
const char* xgw_msg_type_to_string(uint8_t msg_type)
{
    switch (msg_type) {
        case XGW_MSG_TYPE_MOTOR_STATE:
            return "MOTOR_STATE";
        case XGW_MSG_TYPE_MOTOR_CMD:
            return "MOTOR_CMD";
        case XGW_MSG_TYPE_IMU_STATE:
            return "IMU_STATE";
        case XGW_MSG_TYPE_RS485_DATA:
            return "RS485_DATA";
        case XGW_MSG_TYPE_XGW_DIAG:
            return "XGW_DIAG";
        case XGW_MSG_TYPE_XGW_CONFIG:
            return "XGW_CONFIG";
        case XGW_MSG_TYPE_MOTOR_SET:
            return "MOTOR_SET";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Print packet information (for debugging)
 * @param header Packet header
 *
 * Note: Only compiled when XGW_PROTOCOL_ENABLE_LOG is defined
 * On bare metal (Core3), this function is compiled out completely
 * [MIGRATED FROM draft/ccu_ti/xgw_protocol.c:123]
 */
void xgw_print_header(const xgw_header_t* header)
{
#if XGW_PROTOCOL_ENABLE_LOG
    #if XGW_PROTOCOL_ENABLE_VALIDATION
        if (header == NULL) {
            XGW_LOG("[XGW Protocol] ERROR: NULL header in print function\r\n");
            return;
        }
    #endif

    XGW_LOG("========================================\r\n");
    XGW_LOG("xGW Packet Header:\r\n");
    XGW_LOG("  Magic:       0x%04X\r\n", header->magic);
    XGW_LOG("  Version:     %d\r\n", header->version);
    XGW_LOG("  Msg Type:    %s (0x%02X)\r\n",
           xgw_msg_type_to_string(header->msg_type), header->msg_type);
    XGW_LOG("  Count:       %d\r\n", header->count);
    XGW_LOG("  Sequence:    %u\r\n", header->sequence);
    XGW_LOG("  Payload Len: %u bytes\r\n", header->payload_len);
    XGW_LOG("  Timestamp:   %llu ns\r\n", header->timestamp_ns);
    XGW_LOG("  CRC32:       0x%08X\r\n", header->crc32);
    XGW_LOG("========================================\r\n");
#else
    /* Bare metal: Function is a no-op (compiled to nothing) */
    (void)header;  /* Suppress unused parameter warning */
#endif
}
