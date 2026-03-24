/**
 * @file xgw_protocol.c
 * @brief xGW-Linux UDP Protocol v2.0 implementation
 *
 * Protocol helper functions for xGW communication
 * Multi-core compatible version
 */

#include "xgw_protocol.h"
#include "crc32.h"
#include "kernel/dpl/DebugP.h"
#include <string.h>

/*==============================================================================
 * INTERNAL CONSTANTS
 *============================================================================*/

/* CRC32-IEEE (CRC-32/ISO-HDLC) Polynomial
 * Config: Poly=0x04C11DB7, Init=0xFFFFFFFF, FinalXOR=0xFFFFFFFF, RefIn=True, RefOut=True
 * Standard used in: Ethernet, ZIP, GZIP, PNG, SATA, HDLC, etc.
 */
#define CRC32_POLYNOMIAL  0x04c11db7

/*==============================================================================
 * PUBLIC FUNCTIONS - HEADER MANAGEMENT
 *============================================================================*/

void xgw_header_init(xgw_header_t* header, uint8_t msg_type, uint8_t count,
                     uint32_t payload_len, uint32_t sequence)
{
    if (header == NULL) {
        return;
    }

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

uint32_t xgw_crc32_calculate(const xgw_header_t* header, const void* payload, uint32_t payload_len)
{
    if (header == NULL) {
        return 0;
    }

    /* Calculate total size in bytes */
    uint32_t total_bytes = sizeof(xgw_header_t) + payload_len;
    uint32_t total_words = (total_bytes + 3) / 4;

    /* Stack buffer size increased to 1024 bytes (256 * 4) for multi-core compatibility
     * Maximum xGW packet size: 32 (header) + 552 (23 motor commands) = 584 bytes
     * This avoids dynamic memory allocation which is problematic on Core 1 (NoRTOS) */
    #define CRC32_BUFFER_SIZE_WORDS 256  /* 1024 bytes */

    if (total_words > CRC32_BUFFER_SIZE_WORDS) {
        DebugP_log("[XGW Protocol] Packet too large for CRC calculation: %u bytes\r\n", total_bytes);
        return 0;
    }

    /* Allocate temporary buffer on stack */
    uint32_t data_buffer[CRC32_BUFFER_SIZE_WORDS];

    /* Copy header and set CRC to 0 for calculation */
    memcpy(data_buffer, header, sizeof(xgw_header_t));
    ((xgw_header_t*)data_buffer)->crc32 = 0;

    /* Copy payload */
    if (payload != NULL && payload_len > 0) {
        memcpy((uint8_t*)data_buffer + sizeof(xgw_header_t), payload, payload_len);
    }

    /* Calculate CRC - pass byte count, crc32_core handles 0x00 padding */
    return crc32_core((const uint8_t*)data_buffer, total_bytes);
}

bool xgw_crc32_validate(const xgw_header_t* header, const void* payload)
{
    if (header == NULL) {
        return false;
    }

    uint32_t calc_crc = xgw_crc32_calculate(header, payload, header->payload_len);
    return (calc_crc == header->crc32);
}

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

void xgw_print_header(const xgw_header_t* header)
{
    if (header == NULL) {
        return;
    }

    DebugP_log("========================================\r\n");
    DebugP_log("xGW Packet Header:\r\n");
    DebugP_log("  Magic:       0x%04X\r\n", header->magic);
    DebugP_log("  Version:     %d\r\n", header->version);
    DebugP_log("  Msg Type:    %s (0x%02X)\r\n",
               xgw_msg_type_to_string(header->msg_type), header->msg_type);
    DebugP_log("  Count:       %d\r\n", header->count);
    DebugP_log("  Sequence:    %u\r\n", header->sequence);
    DebugP_log("  Payload Len: %u bytes\r\n", header->payload_len);
    DebugP_log("  Timestamp:   %llu ns\r\n", header->timestamp_ns);
    DebugP_log("  CRC32:       0x%08X\r\n", header->crc32);
    DebugP_log("========================================\r\n");
}
