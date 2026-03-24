/**
 * @file crc32.h
 * @brief CRC32 calculation utility for xGW Protocol
 *
 * Common CRC32 implementation used by both cores
 * IEEE 802.3 standard (Polynomial: 0xEDB88320 reversed)
 */

#ifndef CRC32_H_
#define CRC32_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calculate CRC32 checksum using table-driven algorithm
 *
 * Fast table-driven CRC32 implementation matching Python's binascii.crc32.
 * Uses IEEE 802.3 polynomial (0xEDB88320 reversed).
 *
 * Performance: ~10-20us for 500 bytes (vs ~200-400us byte-by-byte)
 * Memory: 1KB lookup table (stored in flash)
 * Speedup: ~10-20x faster than byte-by-byte implementation
 *
 * @param ptr Input data pointer (uint8_t*)
 * @param len Length in bytes
 * @return CRC32 value (IEEE 802.3 standard)
 */
uint32_t crc32_core(const uint8_t* ptr, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRC32_H_ */
