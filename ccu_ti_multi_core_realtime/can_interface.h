/**
 * @file can_interface.h
 * @brief CAN Interface for Core 1 (NoRTOS) - AM263Px MCAN
 *
 * @author CCU Multicore Project
 * @date 2026-03-18
 */

#ifndef CAN_INTERFACE_H_
#define CAN_INTERFACE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * CONSTANTS
 * ========================================================================== */

#define NUM_CAN_BUSES         8
#define CAN_RX_QUEUE_SIZE     16

/* ==========================================================================
 * TYPE DEFINITIONS
 * ========================================================================== */

typedef struct {
    uint32_t can_id;         /* CAN ID (11-bit standard or 29-bit extended) */
    uint8_t  dlc;            /* Data Length Code (0-8) */
    uint8_t  data[8];        /* Data payload */
    uint8_t  flags;          /* Flags (bit 0 = extended, bit 1 = RTR) */
} can_frame_t;

typedef void (*can_rx_callback_t)(uint8_t bus_id, const can_frame_t *frame);

typedef struct {
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t error_count;
    bool     is_initialized;
    bool     is_bus_off;
    uint8_t  tx_error_count;
    uint8_t  rx_error_count;
} can_bus_stats_t;

/* ==========================================================================
 * PUBLIC API
 * ========================================================================== */

void CAN_Init(void);
int32_t CAN_RegisterRxCallback(can_rx_callback_t callback);
int32_t CAN_Transmit(uint8_t bus_id, const can_frame_t *frame);
int32_t CAN_PrepareTx(uint8_t bus_id, const can_frame_t *frame);
int32_t CAN_TriggerTx(uint8_t bus_id, uint8_t buf_idx);
int32_t CAN_TransmitBatch(uint8_t bus_id, const can_frame_t *frames, uint16_t count);
void CAN_GetStats(uint8_t bus_id, can_bus_stats_t *stats);
bool CAN_IsInitialized(uint8_t bus_id);
int32_t CAN_StartRxInterrupts(void);
bool CAN_IsBusOff(uint8_t bus_id);
int32_t CAN_RecoverFromBusOff(uint8_t bus_id);
int32_t CAN_GetErrorCounters(uint8_t bus_id, uint8_t* tx_err, uint8_t* rx_err);

#ifdef __cplusplus
}
#endif

#endif /* CAN_INTERFACE_H_ */
