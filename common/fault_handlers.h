/**
 * @file fault_handlers.h
 * @brief ARM Fault Handlers for CCU Multicore Gateway (Shared Module)
 *
 * Overrides default weak fault handlers from SDK to provide:
 * - Data Abort handler (HardFault equivalent)
 * - Prefetch Abort handler
 * - Undefined instruction handler
 *
 * [MIGRATED FROM draft/ccu_ti/fault_handlers.c]
 * [PLACEMENT: SHARED - accessible by both Core0 (RTOS) and Core1 (bare metal)]
 *
 * NOTE: Stack overflow hook is implemented in SDK's port.c
 * NOTE: Malloc failed hook is in freertos_hooks.c (Core0 only)
 *
 * @author Migrated from ccu_ti by Chu Tien Thinh
 * @date 2026-03-27
 */

#ifndef FAULT_HANDLERS_H_
#define FAULT_HANDLERS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*==============================================================================
 * DEPENDENCIES - SDK Fault Register Structures
 *============================================================================*/

/* Forward declarations from SDK HwiP.h */
typedef struct {
    uint32_t status : 5;     /* Fault status bits */
    uint32_t rw : 1;         /* 0=read, 1=write */
    uint32_t fs : 1;         /* Fault status */
    uint32_t domain : 4;     /* Domain field */
    uint32_t                : 5;     /* Reserved */
    uint32_t wnr : 1;        /* Write not read */
    uint32_t                : 15;    /* Reserved */
} DFSR;

typedef struct {
    uint32_t index : 6;      /* Cache index */
    uint32_t side_ext : 1;   /* Side attribute */
    uint32_t                : 25;    /* Reserved */
} ADFSR;

typedef struct {
    uint32_t status : 5;     /* Fault status bits */
    uint32_t                : 3;     /* Reserved */
    uint32_t fs : 1;         /* Fault status */
    uint32_t domain : 4;     /* Domain field */
    uint32_t                : 19;    /* Reserved */
} IFSR;

typedef struct {
    uint32_t index : 6;      /* Cache index */
    uint32_t side_ext : 1;   /* Side attribute */
    uint32_t                : 25;    /* Reserved */
} AIFSR;

/*==============================================================================
 * PUBLIC API - ARM Fault Handlers (SDK Override Functions)
 *============================================================================*/

/**
 * @brief Data Abort handler (HardFault equivalent)
 *
 * Called on invalid memory access (NULL pointer, unaligned access, etc.)
 * [MIGRATED FROM draft/ccu_ti/fault_handlers.c:143]
 *
 * @param dfsr Data Fault Status Register
 * @param adfsr Auxiliary Data Fault Status Register
 * @param DFAR Data Fault Address Register (address that caused fault)
 * @param LR Link Register (return address)
 * @param SPSR Saved Program Status Register
 */
void HwiP_user_data_abort_handler_c(DFSR dfsr, ADFSR adfsr,
                                     volatile uint32_t DFAR,
                                     volatile uint32_t LR,
                                     volatile uint32_t SPSR);

/**
 * @brief Prefetch Abort handler
 *
 * Called on instruction fetch error (invalid code address)
 * [MIGRATED FROM draft/ccu_ti/fault_handlers.c:194]
 *
 * @param ifsr Instruction Fault Status Register
 * @param aifsr Auxiliary Instruction Fault Status Register
 * @param IFAR Instruction Fault Address Register
 * @param LR Link Register (return address)
 * @param SPSR Saved Program Status Register
 */
void HwiP_user_prefetch_abort_handler_c(IFSR ifsr, AIFSR aifsr,
                                         volatile uint32_t IFAR,
                                         volatile uint32_t LR,
                                         volatile uint32_t SPSR);

/**
 * @brief Undefined instruction handler
 *
 * Called when CPU encounters undefined instruction
 * [MIGRATED FROM draft/ccu_ti/fault_handlers.c:228]
 *
 * @param LR Link Register (return address)
 * @param SPSR Saved Program Status Register
 */
void HwiP_user_undefined_handler_c(volatile uint32_t LR, volatile uint32_t SPSR);

/*==============================================================================
 * CORE IDENTIFICATION UTILITY
 *============================================================================*/

/**
 * @brief Get current core ID (0 or 1)
 *
 * For AM263P dual-core R5F cluster:
 * - Returns 0 for Core0 (FreeRTOS)
 * - Returns 1 for Core1 (bare metal)
 *
 * Implementation reads CPU ID register via inline assembly.
 *
 * @return Core ID (0 or 1)
 */
uint32_t fault_handlers_get_core_id(void);

#ifdef __cplusplus
}
#endif

#endif /* FAULT_HANDLERS_H_ */
