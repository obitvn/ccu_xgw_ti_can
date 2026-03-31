/**
 * @file can_interface.c
 * @brief CAN Interface for Core 1 (NoRTOS) - AM263Px MCAN
 *
 * Ported from ccu_ti for multicore project
 * NoRTOS version - removes FreeRTOS dependencies
 *
 * @author CCU Multicore Project
 * @date 2026-03-18
 */

#include "can_interface.h"
#include "ti_drivers_config.h"
#include "can_config.h"
#include <drivers/mcan.h>
#include <drivers/gpio.h>
#include <kernel/dpl/AddrTranslateP.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/HwiP.h>
#include <string.h>
#include "../gateway_shared.h"

/* Fallback for MCAN_STATUS_E_TIMEOUT if not defined in SDK */
#ifndef MCAN_STATUS_E_TIMEOUT
#define MCAN_STATUS_E_TIMEOUT  (-10)  /* Generic timeout error */
#endif

/* ==========================================================================
 *                      DEBUG COUNTERS (QA TRACE)
 * ==========================================================================
 * Debug counters placed in shared memory for JTAG visibility.
 * Defined here for can_interface.c instrumentation.
 */
volatile uint32_t dbg_can_rx_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;
volatile uint32_t dbg_can_tx_count __attribute__((section(".bss.user_shared_mem_debug"))) = 0;

/* ==========================================================================
 *                      HELPER FUNCTIONS
 * ========================================================================== */

/**
 * @brief Fast GPIO toggle for debug instrumentation
 *
 * @param baseAddr GPIO base address
 * @param pin GPIO pin number
 *
 * Used by QA trace points for minimal overhead (~50ns)
 * Inline function for performance
 *
 * Uses TI SDK GPIO API: GPIO_pinRead, GPIO_pinWriteHigh, GPIO_pinWriteLow
 */
static inline void GPIO_pinToggle(uint32_t baseAddr, uint32_t pin)
{
    /* Toggle using TI SDK GPIO API */
    if (GPIO_pinRead(baseAddr, pin)) {
        GPIO_pinWriteLow(baseAddr, pin);
    } else {
        GPIO_pinWriteHigh(baseAddr, pin);
    }
}

/* ==========================================================================
 *                      Constants and Definitions
 * ========================================================================== */

// Debug GPIO pins for tracing execution (use existing STB GPIOs as indicators)
#define DEBUG_GPIO_TRACE_BASE     CONFIG_GPIO_STB_MCAN0_BASE_ADDR
#define DEBUG_GPIO_TRACE_PIN      CONFIG_GPIO_STB_MCAN0_PIN

// MCAN interrupt numbers (from syscfg generated config)
static const uint32_t g_mcan_intr_num[NUM_CAN_BUSES] = {
    CONFIG_MCAN0_INTR,
    CONFIG_MCAN1_INTR,
    CONFIG_MCAN2_INTR,
    CONFIG_MCAN3_INTR,
    CONFIG_MCAN4_INTR,
    CONFIG_MCAN5_INTR,
    CONFIG_MCAN6_INTR,
    CONFIG_MCAN7_INTR,
};

// MCAN base addresses (from syscfg generated config)
static const uint32_t g_mcan_base_addr[NUM_CAN_BUSES] = {
    CONFIG_MCAN0_BASE_ADDR,
    CONFIG_MCAN1_BASE_ADDR,
    CONFIG_MCAN2_BASE_ADDR,
    CONFIG_MCAN3_BASE_ADDR,
    CONFIG_MCAN4_BASE_ADDR,
    CONFIG_MCAN5_BASE_ADDR,
    CONFIG_MCAN6_BASE_ADDR,
    CONFIG_MCAN7_BASE_ADDR,
};

// MCAN SOC RCM peripheral IDs for clock enable
// Note: IDs are not sequential due to other peripherals in between
static const uint32_t g_mcan_rcm_id[NUM_CAN_BUSES] = {
    SOC_RcmPeripheralId_MCAN0,  // 0
    SOC_RcmPeripheralId_MCAN1,  // 1
    SOC_RcmPeripheralId_MCAN2,  // 2
    SOC_RcmPeripheralId_MCAN3,  // 3
    SOC_RcmPeripheralId_MCAN4,  // 30 (after MCSPI7)
    SOC_RcmPeripheralId_MCAN5,  // 31
    SOC_RcmPeripheralId_MCAN6,  // 32
    SOC_RcmPeripheralId_MCAN7,  // 33
};

/* ==========================================================================
 *                      Global Variables
 * ========================================================================== */

// CAN bus states
static can_bus_stats_t g_can_stats[NUM_CAN_BUSES];
static MCAN_TxBufElement g_tx_buffers[NUM_CAN_BUSES];
static uint8_t g_tx_buffer_index[NUM_CAN_BUSES];

// RX interrupt handling
static HwiP_Object g_mcan_hwi_objects[NUM_CAN_BUSES];
static volatile bool g_rx_interrupts_enabled = false;
static can_rx_callback_t g_rx_callback = NULL;

// RX buffer for reading from message RAM
static MCAN_RxBufElement g_rx_buffer;

/* ==========================================================================
 *                      Internal Functions
 * ========================================================================== */

/**
 * @brief Initialize STB (Standby) GPIO pins for CAN transceivers
 *
 * Uses GPIO configuration from SysConfig (ti_drivers_config.h)
 */
static void init_can_stb_gpios(void)
{
    uint32_t baseAddr;

    DebugP_log("[CAN] Initializing STB GPIO pins...\r\n");

    // MCAN0 STB - MMC_SDCD
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN0_BASE_ADDR);
    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN0_PIN, GPIO_DIRECTION_OUTPUT);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN0_PIN);

    // MCAN1 STB - MMC_SDWP
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN1_BASE_ADDR);
    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN1_PIN, CONFIG_GPIO_STB_MCAN1_DIR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN1_PIN);

    // MCAN2 STB - EQEP0_A
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN2_BASE_ADDR);
    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN2_PIN, CONFIG_GPIO_STB_MCAN2_DIR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN2_PIN);

    // MCAN3 STB - EPWM1_A
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN3_BASE_ADDR);
    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN3_PIN, CONFIG_GPIO_STB_MCAN3_DIR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN3_PIN);

    // MCAN4 STB - EPWM0_A
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN4_BASE_ADDR);
    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN4_PIN, CONFIG_GPIO_STB_MCAN4_DIR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN4_PIN);

    // MCAN5 STB - EPWM9_B
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN5_BASE_ADDR);
    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN5_PIN, CONFIG_GPIO_STB_MCAN5_DIR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN5_PIN);

    // MCAN6 STB - EPWM0_B
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN6_BASE_ADDR);
    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN6_PIN, CONFIG_GPIO_STB_MCAN6_DIR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN6_PIN);

    // MCAN7 STB - EPWM13_B
    baseAddr = (uint32_t)AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN7_BASE_ADDR);
    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN7_PIN, CONFIG_GPIO_STB_MCAN7_DIR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN7_PIN);

    DebugP_log("[CAN] All STB GPIO pins initialized (LOW = enabled)\r\n");
}

/**
 * @brief MCAN RX Interrupt Handler
 * Handles RX FIFO 0 and Bus-Off detection
 */
static void can_rx_isr(void *arg)
{
    // [QA TRACE T021] GPIO PA2 toggle on ISR entry
    /* TODO: Enable GPIO instrumentation pins in SysConfig before uncommenting
    uint32_t debug_gpio_base = (uint32_t)AddrTranslateP_getLocalAddr(CSL_GPIO3_U_BASE);
    GPIO_pinToggle(debug_gpio_base, 42U);  // PA2 = CAN RX indicator
    */

    uint32_t bus_id = (uint32_t)arg;
    uint32_t baseAddr = g_mcan_base_addr[bus_id];
    uint32_t intrStatus;
    MCAN_RxNewDataStatus newDataStatus;

    // Get interrupt status
    intrStatus = MCAN_getIntrStatus(baseAddr);

    // Clear interrupt status FIRST
    MCAN_clearIntrStatus(baseAddr, intrStatus);

    /* BUS-OFF INTERRUPT HANDLING */
    if (intrStatus & MCAN_INTR_SRC_BUS_OFF_STATUS)
    {
        /* Bug B008 Fix: Atomic write with memory barrier */
        g_can_stats[bus_id].is_bus_off = true;
        __asm volatile("dmb" ::: "memory");  /* Ensure visibility to main loop */
        DebugP_logError("[CAN] ISR: CAN%d Bus-Off detected!\r\n", bus_id);
    }

    /* NORMAL RX INTERRUPT HANDLING */
    if (intrStatus & MCAN_INTR_SRC_RX_FIFO0_NEW_MSG)
    {
        // Get and clear new data status
        MCAN_getNewDataStatus(baseAddr, &newDataStatus);
        newDataStatus.statusLow = 0x1U;
        MCAN_clearNewDataStatus(baseAddr, &newDataStatus);

        // Read message from FIFO 0
        MCAN_readMsgRam(baseAddr, MCAN_MEM_TYPE_FIFO, 0, MCAN_RX_FIFO_NUM_0, &g_rx_buffer);

        // Update statistics
        /* Bug B008 Fix: Atomic increment with memory barrier
         * While 32-bit aligned writes are atomic on ARM, ++ is read-modify-write.
         * In bare-metal context, ISR has exclusive access during execution,
         * but DMB ensures visibility to main loop when it reads stats.
         */
        g_can_stats[bus_id].rx_count++;
        __asm volatile("dmb" ::: "memory");  /* Ensure visibility to main loop */

        /* [QA TRACE T025] Increment CAN RX counter */
        DEBUG_COUNTER_INC(dbg_can_rx_count);

        // Call user callback if registered
        if (g_rx_callback != NULL)
        {
            can_frame_t frame;

            // Convert MCAN format to our format
            if (g_rx_buffer.xtd)
            {
                frame.can_id = g_rx_buffer.id & 0x1FFFFFFF;
                frame.flags = 0x01;
            }
            else
            {
                frame.can_id = (g_rx_buffer.id >> 18) & 0x7FF;
                frame.flags = 0x00;
            }

            frame.dlc = g_rx_buffer.dlc;
            if (frame.dlc > 8) frame.dlc = 8;

            for (uint8_t i = 0; i < frame.dlc; i++)
            {
                frame.data[i] = g_rx_buffer.data[i];
            }

            g_rx_callback(bus_id, &frame);
        }
    }

    // [QA TRACE T021] GPIO PA2 toggle on ISR exit
    /* TODO: Enable GPIO instrumentation pins in SysConfig before uncommenting
    GPIO_pinToggle(debug_gpio_base, 42U);  // PA2 = CAN RX indicator
    */
}

/**
 * @brief Initialize a single MCAN peripheral
 *
 * NOTE: MCAN is configured in example.syscfg but Drivers_mcanOpen()
 * does not auto-generate initialization code. MCAN clocks must be enabled
 * manually via CSL API or the MCAN driver will hang on MCAN_isMemInitDone().
 *
 * FIX: Removed MCAN_isMemInitDone() check - SDK examples don't use it.
 * The MCAN peripheral is ready after Drivers_open() for direct configuration.
 */
static int32_t init_single_mcan(uint8_t bus_id)
{
    uint32_t baseAddr = g_mcan_base_addr[bus_id];
    int32_t ret;

    DebugP_log("[CAN] Initializing CAN%d (base: 0x%08X)...\r\n", bus_id, baseAddr);
    DebugP_log("[CAN] CAN%d: About to set SW_INIT mode...\r\n", bus_id);

    // Enable MCAN peripheral clock
    // MCAN4-7 are in different clock domains and need explicit enable
    uint32_t rcmId = g_mcan_rcm_id[bus_id];
    DebugP_log("[CAN] CAN%d: Enabling clock (rcmId=%d)...\r\n", bus_id, rcmId);
    ret = SOC_moduleClockEnable(rcmId, 1);
    if (ret != SystemP_SUCCESS) {
        DebugP_log("[CAN] ERROR: Failed to enable clock for CAN%d (rcmId=%d)\r\n", bus_id, rcmId);
        return ret;
    }
    DebugP_log("[CAN] CAN%d: Clock enabled successfully\r\n", bus_id);

    // Small delay after clock enable to ensure peripheral is ready
    volatile uint32_t delay = 1000;
    while (delay--) {
        __asm__("nop");
    }

    DebugP_log("[CAN] CAN%d: About to call MCAN_setOpMode(SW_INIT)...\r\n", bus_id);
    // Set to Software Initialization mode
    MCAN_setOpMode(baseAddr, MCAN_OPERATION_MODE_SW_INIT);
    DebugP_log("[CAN] CAN%d: MCAN_setOpMode returned, about to wait for mode switch...\r\n", bus_id);

    // [FIX] Add timeout protection for mode switch
    uint32_t timeout = 100000;
    while (MCAN_OPERATION_MODE_SW_INIT != MCAN_getOpMode(baseAddr) && timeout > 0) {
        timeout--;
    }
    if (timeout == 0) {
        DebugP_log("[CAN] ERROR: CAN%d timeout waiting for SW_INIT mode!\r\n", bus_id);
        DebugP_log("[CAN] Current mode: 0x%X, expected: 0x%X\r\n",
                   MCAN_getOpMode(baseAddr), MCAN_OPERATION_MODE_SW_INIT);
        return MCAN_STATUS_E_TIMEOUT;
    }
    DebugP_log("[CAN] CAN%d: SW_INIT mode confirmed\r\n", bus_id);

    // Initialize MCAN for CAN 2.0 mode
    MCAN_InitParams initParams;
    memset(&initParams, 0, sizeof(initParams));

    initParams.fdMode = 0;
    initParams.brsEnable = 0;
    initParams.txpEnable = 0;
    initParams.efbi = 0;
    initParams.pxhddisable = 0;
    initParams.darEnable = 0;
    initParams.emulationEnable = 1;

    ret = MCAN_init(baseAddr, &initParams);
    if (ret != MCAN_STATUS_SUCCESS) {
        DebugP_log("[CAN] ERROR: MCAN_init failed for CAN%d (ret=%d)\r\n", bus_id, ret);
        return ret;
    }

    // Configure MCAN
    MCAN_ConfigParams configParams;
    memset(&configParams, 0, sizeof(configParams));

    configParams.monEnable = 0;
    configParams.tsPrescalar = 0;
    configParams.tsSelect = 0;
    configParams.timeoutSelect = MCAN_TIMEOUT_SELECT_CONT;
    configParams.timeoutCntEnable = 0;

    // Accept all messages
    configParams.filterConfig.rrfe = 0;
    configParams.filterConfig.rrfs = 0;
    configParams.filterConfig.anfe = 0;
    configParams.filterConfig.anfs = 0;

    ret = MCAN_config(baseAddr, &configParams);
    if (ret != MCAN_STATUS_SUCCESS) {
        DebugP_log("[CAN] ERROR: MCAN_config failed for CAN%d (ret=%d)\r\n", bus_id, ret);
        return ret;
    }

    DebugP_log("[CAN] CAN%d config done, starting bit timing...\r\n", bus_id);

    // Set bit timing for 1 Mbps @ 80 MHz MCAN clock
    MCAN_BitTimingParams bitTiming;
    memset(&bitTiming, 0, sizeof(bitTiming));

    bitTiming.nomRatePrescalar = 3;
    bitTiming.nomTimeSeg1 = 14;
    bitTiming.nomTimeSeg2 = 3;
    bitTiming.nomSynchJumpWidth = 3;

    ret = MCAN_setBitTime(baseAddr, &bitTiming);
    if (ret != MCAN_STATUS_SUCCESS) {
        DebugP_log("[CAN] ERROR: MCAN_setBitTime failed for CAN%d (ret=%d)\r\n", bus_id, ret);
        return ret;
    }

    // Configure Message RAM
    MCAN_MsgRAMConfigParams msgRamConfig;
    memset(&msgRamConfig, 0, sizeof(msgRamConfig));

    msgRamConfig.lss = 1;
    msgRamConfig.lse = 0;
    msgRamConfig.txBufCnt = 32;
    msgRamConfig.txBufMode = 0;
    msgRamConfig.rxFIFO0Cnt = 64;
    msgRamConfig.rxFIFO0OpMode = MCAN_RX_FIFO_OPERATION_MODE_OVERWRITE;
    msgRamConfig.rxFIFO0ElemSize = MCAN_ELEM_SIZE_8BYTES;

    MCAN_calcMsgRamParamsStartAddr(&msgRamConfig);

    ret = MCAN_msgRAMConfig(baseAddr, &msgRamConfig);
    if (ret != MCAN_STATUS_SUCCESS) {
        DebugP_log("[CAN] ERROR: MCAN_msgRAMConfig failed for CAN%d (ret=%d)\r\n", bus_id, ret);
        return ret;
    }

    // Configure Standard ID Filter
    MCAN_StdMsgIDFilterElement stdFilter;
    stdFilter.sfid1 = 0x000;
    stdFilter.sfid2 = 0x7FF;
    stdFilter.sfec = MCAN_STD_FILT_ELEM_FIFO0;
    stdFilter.sft = MCAN_STD_FILT_TYPE_RANGE;
    MCAN_addStdMsgIDFilter(baseAddr, 0, &stdFilter);

    // Enable Interrupts
    MCAN_enableIntr(baseAddr, MCAN_INTR_MASK_ALL, 1);
    MCAN_enableIntr(baseAddr, MCAN_INTR_SRC_RES_ADDR_ACCESS, 0);
    MCAN_selectIntrLine(baseAddr, MCAN_INTR_MASK_ALL, MCAN_INTR_LINE_NUM_0);
    MCAN_enableIntrLine(baseAddr, MCAN_INTR_LINE_NUM_0, 1);

    // Initialize TX buffer
    MCAN_initTxBufElement(&g_tx_buffers[bus_id]);
    g_tx_buffer_index[bus_id] = 0;

    // Set to Normal operation mode
    MCAN_setOpMode(baseAddr, MCAN_OPERATION_MODE_NORMAL);

    // [FIX] Add timeout protection for mode switch
    timeout = 100000;
    while (MCAN_OPERATION_MODE_NORMAL != MCAN_getOpMode(baseAddr) && timeout > 0) {
        timeout--;
    }
    if (timeout == 0) {
        DebugP_log("[CAN] ERROR: CAN%d timeout waiting for NORMAL mode!\r\n", bus_id);
        return MCAN_STATUS_E_TIMEOUT;
    }

    DebugP_log("[CAN] CAN%d initialized successfully (1 Mbps, CAN 2.0)\r\n", bus_id);
    return MCAN_STATUS_SUCCESS;
}

/**
 * @brief Get current PSR register
 */
static inline uint32_t get_psr_register(uint32_t baseAddr)
{
    return HW_RD_REG32(baseAddr + 0x44);
}

/* ==========================================================================
 *                      Public API Implementation
 * ========================================================================== */

void CAN_Init(void)
{
    DebugP_log("\r\n");
    DebugP_log("========================================\r\n");
    DebugP_log("  CAN Interface Initialization (Core1)\r\n");
    DebugP_log("========================================\r\n");
    DebugP_log("Initializing %d CAN buses (MCAN0-7)...\r\n", NUM_CAN_BUSES);

    init_can_stb_gpios();

    for (uint8_t i = 0; i < NUM_CAN_BUSES; i++) {
        memset(&g_can_stats[i], 0, sizeof(can_bus_stats_t));

        int32_t ret = init_single_mcan(i);
        if (ret == MCAN_STATUS_SUCCESS) {
            g_can_stats[i].is_initialized = true;
        } else {
            g_can_stats[i].is_initialized = false;
            DebugP_log("[CAN] WARNING: CAN%d initialization failed!\r\n", i);
        }
    }

    uint32_t initialized_count = 0;
    for (uint8_t i = 0; i < NUM_CAN_BUSES; i++) {
        if (g_can_stats[i].is_initialized) {
            initialized_count++;
        }
    }

    DebugP_log("\r\n[CAN] Initialization Complete: %d/%d buses ready\r\n",
              initialized_count, NUM_CAN_BUSES);
    DebugP_log("========================================\r\n\r\n");
}

int32_t CAN_RegisterRxCallback(can_rx_callback_t callback)
{
    if (callback == NULL) {
        DebugP_log("[CAN] ERROR: callback is NULL!\r\n");
        return -1;
    }

    g_rx_callback = callback;
    DebugP_log("[CAN] RX callback registered\r\n");
    return 0;
}

int32_t CAN_StartRxInterrupts(void)
{
    int32_t ret;
    HwiP_Params hwiParams;

    DebugP_log("\r\n[CAN] Starting RX interrupts...\r\n");
    DebugP_log("[CAN] About to call HwiP_Params_init...\r\n");

    HwiP_Params_init(&hwiParams);

    DebugP_log("[CAN] HwiP_Params_init done, about to loop through %d CAN buses...\r\n", NUM_CAN_BUSES);

    for (uint8_t i = 0; i < NUM_CAN_BUSES; i++) {
        DebugP_log("[CAN] Loop iteration: CAN%d, is_initialized=%d\r\n", i, g_can_stats[i].is_initialized);
        if (!g_can_stats[i].is_initialized) {
            DebugP_log("[CAN] Skipping CAN%d (not initialized)\r\n", i);
            continue;
        }

        DebugP_log("[CAN] CAN%d: Setting up ISR params: intNum=%d\r\n", i, g_mcan_intr_num[i]);
        hwiParams.intNum = g_mcan_intr_num[i];
        hwiParams.callback = &can_rx_isr;
        hwiParams.args = (void*)(uint32_t)i;

        DebugP_log("[CAN] CAN%d: About to call HwiP_construct...\r\n", i);
        ret = HwiP_construct(&g_mcan_hwi_objects[i], &hwiParams);
        DebugP_log("[CAN] CAN%d: HwiP_construct returned (ret=%d)\r\n", i, ret);

        if (ret != SystemP_SUCCESS) {
            DebugP_log("[CAN] ERROR: Failed to register ISR for CAN%d (ret=%d)\r\n", i, ret);
            return -1;
        }

        DebugP_log("[CAN] RX ISR registered for CAN%d (IRQ %d)\r\n", i, g_mcan_intr_num[i]);
    }

    g_rx_interrupts_enabled = true;
    DebugP_log("[CAN] RX interrupt handlers ready\r\n");

    return 0;
}

int32_t CAN_Transmit(uint8_t bus_id, const can_frame_t *frame)
{
    if (bus_id >= NUM_CAN_BUSES || !g_can_stats[bus_id].is_initialized) {
        return -1;
    }

    if (frame == NULL || frame->dlc > 8) {
        return -1;
    }

    uint32_t baseAddr = g_mcan_base_addr[bus_id];
    MCAN_TxBufElement *txElement = &g_tx_buffers[bus_id];

    // Prepare TX element
    if (frame->flags & 0x01) {
        txElement->id = frame->can_id & 0x1FFFFFFF;
        txElement->xtd = 1;
    } else {
        txElement->id = (frame->can_id & 0x7FF) << 18;
        txElement->xtd = 0;
    }

    txElement->dlc = frame->dlc;
    txElement->rtr = (frame->flags & 0x02) ? 1 : 0;
    txElement->fdf = 0;
    txElement->brs = 0;
    txElement->esi = 0;
    txElement->efc = 0;
    txElement->mm = 0;

    /* Bug B012 fix: Add explicit bounds check to prevent buffer overflow */
    for (uint8_t i = 0; i < frame->dlc; i++) {
        if (i >= 8) break;  /* Explicit bounds check: CAN data array max 8 bytes */
        txElement->data[i] = frame->data[i];
    }

    uint8_t buf_idx = g_tx_buffer_index[bus_id];
    MCAN_writeMsgRam(baseAddr, MCAN_MEM_TYPE_BUF, buf_idx, txElement);

    int32_t ret = MCAN_txBufAddReq(baseAddr, buf_idx);

    if (ret == MCAN_STATUS_SUCCESS) {
        /* Bug B008 Fix: Disable IRQ to protect stats update from concurrent ISR access */
        uint32_t irq_state = HwiP_disable();
        g_can_stats[bus_id].tx_count++;
        HwiP_restore(irq_state);

        /* [QA TRACE T026] Increment CAN TX counter */
        DEBUG_COUNTER_INC(dbg_can_tx_count);
        g_tx_buffer_index[bus_id] = (buf_idx + 1) % 32;
        return 0;
    } else {
        /* Bug B008 Fix: Disable IRQ to protect stats update from concurrent ISR access */
        uint32_t irq_state = HwiP_disable();
        g_can_stats[bus_id].error_count++;
        HwiP_restore(irq_state);
        return -1;
    }
}

int32_t CAN_TransmitBatch(uint8_t bus_id, const can_frame_t *frames, uint16_t count)
{
    if (bus_id >= NUM_CAN_BUSES || !g_can_stats[bus_id].is_initialized) {
        return -1;
    }
    if (frames == NULL || count == 0) {
        return 0;
    }

    uint32_t baseAddr = g_mcan_base_addr[bus_id];
    uint8_t buf_idx = g_tx_buffer_index[bus_id];
    MCAN_TxBufElement *txElement = &g_tx_buffers[bus_id];
    uint16_t success_count = 0;

    for (uint16_t i = 0; i < count; i++) {
        const can_frame_t *frame = &frames[i];

        if (frame->dlc > 8) {
            continue;
        }

        if (frame->flags & 0x01) {
            txElement->id = frame->can_id & 0x1FFFFFFF;
            txElement->xtd = 1;
        } else {
            txElement->id = (frame->can_id & 0x7FF) << 18;
            txElement->xtd = 0;
        }

        txElement->dlc = frame->dlc;
        txElement->rtr = (frame->flags & 0x02) ? 1 : 0;
        txElement->fdf = 0;
        txElement->brs = 0;
        txElement->esi = 0;
        txElement->efc = 0;
        txElement->mm = 0;

        /* Bug B012 fix: Add explicit bounds check to prevent buffer overflow */
        /* Also fixes D013: Variable shadowing - inner loop uses byte_idx instead of i */
        for (uint8_t byte_idx = 0; byte_idx < frame->dlc; byte_idx++) {
            if (byte_idx >= 8) break;  /* Explicit bounds check: CAN data array max 8 bytes */
            txElement->data[byte_idx] = frame->data[byte_idx];
        }

        MCAN_writeMsgRam(baseAddr, MCAN_MEM_TYPE_BUF, buf_idx, txElement);

        int32_t ret = MCAN_txBufAddReq(baseAddr, buf_idx);

        if (ret == MCAN_STATUS_SUCCESS) {
            success_count++;
            buf_idx = (buf_idx + 1) % 32;
        } else {
            /* Bug B008 Fix: Disable IRQ to protect stats update from concurrent ISR access */
            uint32_t irq_state = HwiP_disable();
            g_can_stats[bus_id].error_count++;
            HwiP_restore(irq_state);
        }
    }

    /* Bug B008 Fix: Disable IRQ to protect stats update from concurrent ISR access */
    uint32_t irq_state = HwiP_disable();
    g_can_stats[bus_id].tx_count += success_count;
    HwiP_restore(irq_state);
    g_tx_buffer_index[bus_id] = buf_idx;

    return success_count;
}

int32_t CAN_PrepareTx(uint8_t bus_id, const can_frame_t *frame)
{
    if (bus_id >= NUM_CAN_BUSES || !g_can_stats[bus_id].is_initialized) {
        return -1;
    }
    if (frame == NULL || frame->dlc > 8) {
        return -1;
    }

    uint32_t baseAddr = g_mcan_base_addr[bus_id];
    MCAN_TxBufElement *txElement = &g_tx_buffers[bus_id];
    uint8_t buf_idx = g_tx_buffer_index[bus_id];

    if (frame->flags & 0x01) {
        txElement->id = frame->can_id & 0x1FFFFFFF;
        txElement->xtd = 1;
    } else {
        txElement->id = (frame->can_id & 0x7FF) << 18;
        txElement->xtd = 0;
    }

    txElement->dlc = frame->dlc;
    txElement->rtr = (frame->flags & 0x02) ? 1 : 0;
    txElement->fdf = 0;
    txElement->brs = 0;
    txElement->esi = 0;
    txElement->efc = 0;
    txElement->mm = 0;

    /* Bug B012 fix: Add explicit bounds check to prevent buffer overflow */
    for (uint8_t i = 0; i < frame->dlc; i++) {
        if (i >= 8) break;  /* Explicit bounds check: CAN data array max 8 bytes */
        txElement->data[i] = frame->data[i];
    }

    MCAN_writeMsgRam(baseAddr, MCAN_MEM_TYPE_BUF, buf_idx, txElement);

    g_tx_buffer_index[bus_id] = (buf_idx + 1) % 32;

    return buf_idx;
}

int32_t CAN_TriggerTx(uint8_t bus_id, uint8_t buf_idx)
{
    if (bus_id >= NUM_CAN_BUSES || !g_can_stats[bus_id].is_initialized) {
        return -1;
    }
    if (buf_idx >= 32) {
        return -1;
    }

    uint32_t baseAddr = g_mcan_base_addr[bus_id];
    int32_t ret = MCAN_txBufAddReq(baseAddr, buf_idx);

    if (ret == MCAN_STATUS_SUCCESS) {
        /* Bug B008 Fix: Disable IRQ to protect stats update from concurrent ISR access */
        uint32_t irq_state = HwiP_disable();
        g_can_stats[bus_id].tx_count++;
        HwiP_restore(irq_state);
        return 0;
    } else {
        /* Bug B008 Fix: Disable IRQ to protect stats update from concurrent ISR access */
        uint32_t irq_state = HwiP_disable();
        g_can_stats[bus_id].error_count++;
        HwiP_restore(irq_state);
        return -1;
    }
}

void CAN_GetStats(uint8_t bus_id, can_bus_stats_t *stats)
{
    if (bus_id < NUM_CAN_BUSES && stats != NULL) {
        /* Bug B008 Fix: Disable IRQ during stats read to prevent torn reads
         * g_can_stats[] is modified by CAN RX ISR (lines 162, 178) and read here.
         * On ARM Cortex-R5F, while 32-bit aligned accesses are atomic, reading
         * the entire struct can see inconsistent state if ISR updates mid-copy.
         * Disable interrupts to ensure atomic read of the entire structure.
         */
        uint32_t irq_state = HwiP_disable();
        memcpy(stats, &g_can_stats[bus_id], sizeof(can_bus_stats_t));
        HwiP_restore(irq_state);
    }
}

bool CAN_IsInitialized(uint8_t bus_id)
{
    return (bus_id < NUM_CAN_BUSES) ? g_can_stats[bus_id].is_initialized : false;
}

bool CAN_IsBusOff(uint8_t bus_id)
{
    if (bus_id >= NUM_CAN_BUSES || !g_can_stats[bus_id].is_initialized) {
        return false;
    }

    uint32_t baseAddr = g_mcan_base_addr[bus_id];
    MCAN_ProtocolStatus protStatus;

    MCAN_getProtocolStatus(baseAddr, &protStatus);

    bool is_bus_off = (protStatus.busOffStatus != 0);
    /* Bug B008 Fix: Disable IRQ to protect stats update from concurrent ISR access */
    uint32_t irq_state = HwiP_disable();
    g_can_stats[bus_id].is_bus_off = is_bus_off;
    HwiP_restore(irq_state);

    return is_bus_off;
}

int32_t CAN_GetErrorCounters(uint8_t bus_id, uint8_t* tx_err, uint8_t* rx_err)
{
    if (bus_id >= NUM_CAN_BUSES || !g_can_stats[bus_id].is_initialized) {
        return -1;
    }

    if (tx_err == NULL || rx_err == NULL) {
        return -1;
    }

    uint32_t baseAddr = g_mcan_base_addr[bus_id];
    uint32_t psr = get_psr_register(baseAddr);

    *tx_err = (psr >> 16) & 0xFF;
    *rx_err = (psr >> 8) & 0xFF;

    /* Bug B008 Fix: Disable IRQ to protect stats update from concurrent ISR access */
    uint32_t irq_state = HwiP_disable();
    g_can_stats[bus_id].tx_error_count = *tx_err;
    g_can_stats[bus_id].rx_error_count = *rx_err;
    HwiP_restore(irq_state);

    return 0;
}

int32_t CAN_RecoverFromBusOff(uint8_t bus_id)
{
    if (bus_id >= NUM_CAN_BUSES || !g_can_stats[bus_id].is_initialized) {
        return -1;
    }

    uint32_t baseAddr = g_mcan_base_addr[bus_id];

    MCAN_setOpMode(baseAddr, MCAN_OPERATION_MODE_SW_INIT);
    while (MCAN_OPERATION_MODE_SW_INIT != MCAN_getOpMode(baseAddr)) {
        // Wait
    }

    MCAN_enableIntr(baseAddr, MCAN_INTR_MASK_ALL, 1);
    MCAN_selectIntrLine(baseAddr, MCAN_INTR_MASK_ALL, MCAN_INTR_LINE_NUM_0);
    MCAN_enableIntrLine(baseAddr, MCAN_INTR_LINE_NUM_0, 1);

    MCAN_setOpMode(baseAddr, MCAN_OPERATION_MODE_NORMAL);
    while (MCAN_OPERATION_MODE_NORMAL != MCAN_getOpMode(baseAddr)) {
        // Wait
    }

    if (CAN_IsBusOff(bus_id)) {
        /* Bug B008 Fix: Disable IRQ to protect stats update from concurrent ISR access */
        uint32_t irq_state = HwiP_disable();
        g_can_stats[bus_id].error_count++;
        HwiP_restore(irq_state);
        return -1;
    }

    return 0;
}

/* Simplified auto-recovery for NoRTOS */
int32_t CAN_StartAutoRecoveryTask(void)
{
    /* Auto-recovery task not available in NoRTOS */
    /* Bus-off recovery will be handled manually */
    DebugP_log("[CAN] Auto-recovery task: NoRTOS mode (manual recovery)\r\n");
    return 0;
}

int32_t CAN_StopAutoRecoveryTask(void)
{
    return 0;
}

bool CAN_IsAutoRecoveryRunning(void)
{
    return false;
}

int32_t CAN_GetRecoveryStats(uint8_t bus_id, can_recovery_stats_t* stats)
{
    (void)bus_id;
    (void)stats;
    return -1;
}
