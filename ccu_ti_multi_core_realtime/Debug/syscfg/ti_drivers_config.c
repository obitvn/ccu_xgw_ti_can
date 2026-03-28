/*
 *  Copyright (C) 2021 Texas Instruments Incorporated
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Auto generated file
 */

#include "ti_drivers_config.h"

/*
 * IPC Notify
 */
#include <drivers/ipc_notify.h>
#include <drivers/ipc_notify/v1/ipc_notify_v1.h>

/* Dedicated mailbox memories address and size */
#define MSS_MBOX_MEM                (CSL_MBOX_SRAM_U_BASE)
#define MSS_MBOX_MEM_SIZE           (16U*1024U)

/*
* SW queue between each pair of CPUs
*
* place SW queues at the bottom of the dedicated mailbox memories.
* Driver assume this memory is init to zero in bootloader as it's ECC protected and
* needs to be intialized only once and to ensure that only one core has done the
* mailbox ram initialization before ipc_init. If SBL is not used then Gel does the initialization.
* We need 4*3 SW Q's for the 4x R5F to send messages to each other, i.e 384 B.
*
* Rest of the mailbox memory cna be used for ipc_rpmessage or custom message passing.
*/
#define R5FSS0_0_TO_R5FSS0_1_SW_QUEUE      (IpcNotify_SwQueue*)((MSS_MBOX_MEM + MSS_MBOX_MEM_SIZE) - (MAILBOX_MAX_SW_QUEUE_SIZE*12U))
#define R5FSS0_0_TO_R5FSS1_0_SW_QUEUE      (IpcNotify_SwQueue*)((MSS_MBOX_MEM + MSS_MBOX_MEM_SIZE) - (MAILBOX_MAX_SW_QUEUE_SIZE*11U))
#define R5FSS0_0_TO_R5FSS1_1_SW_QUEUE      (IpcNotify_SwQueue*)((MSS_MBOX_MEM + MSS_MBOX_MEM_SIZE) - (MAILBOX_MAX_SW_QUEUE_SIZE*10U))
#define R5FSS0_1_TO_R5FSS0_0_SW_QUEUE      (IpcNotify_SwQueue*)((MSS_MBOX_MEM + MSS_MBOX_MEM_SIZE) - (MAILBOX_MAX_SW_QUEUE_SIZE*9U))
#define R5FSS0_1_TO_R5FSS1_0_SW_QUEUE      (IpcNotify_SwQueue*)((MSS_MBOX_MEM + MSS_MBOX_MEM_SIZE) - (MAILBOX_MAX_SW_QUEUE_SIZE*8U))
#define R5FSS0_1_TO_R5FSS1_1_SW_QUEUE      (IpcNotify_SwQueue*)((MSS_MBOX_MEM + MSS_MBOX_MEM_SIZE) - (MAILBOX_MAX_SW_QUEUE_SIZE*7U))
#define R5FSS1_0_TO_R5FSS0_0_SW_QUEUE      (IpcNotify_SwQueue*)((MSS_MBOX_MEM + MSS_MBOX_MEM_SIZE) - (MAILBOX_MAX_SW_QUEUE_SIZE*6U))
#define R5FSS1_0_TO_R5FSS0_1_SW_QUEUE      (IpcNotify_SwQueue*)((MSS_MBOX_MEM + MSS_MBOX_MEM_SIZE) - (MAILBOX_MAX_SW_QUEUE_SIZE*5U))
#define R5FSS1_0_TO_R5FSS1_1_SW_QUEUE      (IpcNotify_SwQueue*)((MSS_MBOX_MEM + MSS_MBOX_MEM_SIZE) - (MAILBOX_MAX_SW_QUEUE_SIZE*4U))
#define R5FSS1_1_TO_R5FSS0_0_SW_QUEUE      (IpcNotify_SwQueue*)((MSS_MBOX_MEM + MSS_MBOX_MEM_SIZE) - (MAILBOX_MAX_SW_QUEUE_SIZE*3U))
#define R5FSS1_1_TO_R5FSS0_1_SW_QUEUE      (IpcNotify_SwQueue*)((MSS_MBOX_MEM + MSS_MBOX_MEM_SIZE) - (MAILBOX_MAX_SW_QUEUE_SIZE*2U))
#define R5FSS1_1_TO_R5FSS1_0_SW_QUEUE      (IpcNotify_SwQueue*)((MSS_MBOX_MEM + MSS_MBOX_MEM_SIZE) - (MAILBOX_MAX_SW_QUEUE_SIZE*1U))

/* This function is called within IpcNotify_init, this function returns core specific IPC config */
void IpcNotify_getConfig(IpcNotify_InterruptConfig **interruptConfig, uint32_t *interruptConfigNum)
{
    /* extern globals that are specific to this core */
    extern IpcNotify_InterruptConfig gIpcNotifyInterruptConfig_r5fss1_1[];
    extern uint32_t gIpcNotifyInterruptConfigNum_r5fss1_1;

    *interruptConfig = &gIpcNotifyInterruptConfig_r5fss1_1[0];
    *interruptConfigNum = gIpcNotifyInterruptConfigNum_r5fss1_1;
}

/* This function is called within IpcNotify_init, this function allocates SW queue */
void IpcNotify_allocSwQueue(IpcNotify_MailboxConfig *mailboxConfig)
{
    IpcNotify_MailboxConfig (*mailboxConfigPtr)[CSL_CORE_ID_MAX] = (void *)mailboxConfig;

    mailboxConfigPtr[CSL_CORE_ID_R5FSS1_1][CSL_CORE_ID_R5FSS0_0].swQ = R5FSS1_1_TO_R5FSS0_0_SW_QUEUE;
    mailboxConfigPtr[CSL_CORE_ID_R5FSS0_0][CSL_CORE_ID_R5FSS1_1].swQ = R5FSS0_0_TO_R5FSS1_1_SW_QUEUE;
}


/*
 * UART
 */

/* UART atrributes */
static UART_Attrs gUartAttrs[CONFIG_UART_NUM_INSTANCES] =
{
        {
            .baseAddr           = CSL_UART5_U_BASE,
            .inputClkFreq       = 48000000U,
        },
};
/* UART objects - initialized by the driver */
static UART_Object gUartObjects[CONFIG_UART_NUM_INSTANCES];
/* UART driver configuration */
UART_Config gUartConfig[CONFIG_UART_NUM_INSTANCES] =
{
        {
            &gUartAttrs[CONFIG_UART5],
            &gUartObjects[CONFIG_UART5],
        },
};

uint32_t gUartConfigNum = CONFIG_UART_NUM_INSTANCES;

#include <drivers/uart/v0/lld/dma/uart_dma.h>
UART_DmaHandle gUartDmaHandle[] =
{
};

uint32_t gUartDmaConfigNum = CONFIG_UART_NUM_DMA_INSTANCES;

void Drivers_uartInit(void)
{
    UART_init();
}
/*
 * GPIO
 */

/* ----------- GPIO Direction, Trigger, Interrupt initialization ----------- */

void GPIO_init()
{
    uint32_t    baseAddr;

    /* Instance 0 */
    /* Get address after translation translate */
    baseAddr = (uint32_t) AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN4_BASE_ADDR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN4_PIN);

    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN4_PIN, CONFIG_GPIO_STB_MCAN4_DIR);
    /* Instance 1 */
    /* Get address after translation translate */
    baseAddr = (uint32_t) AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN3_BASE_ADDR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN3_PIN);

    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN3_PIN, CONFIG_GPIO_STB_MCAN3_DIR);
    /* Instance 2 */
    /* Get address after translation translate */
    baseAddr = (uint32_t) AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN5_BASE_ADDR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN5_PIN);

    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN5_PIN, CONFIG_GPIO_STB_MCAN5_DIR);
    /* Instance 3 */
    /* Get address after translation translate */
    baseAddr = (uint32_t) AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN7_BASE_ADDR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN7_PIN);

    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN7_PIN, CONFIG_GPIO_STB_MCAN7_DIR);
    /* Instance 4 */
    /* Get address after translation translate */
    baseAddr = (uint32_t) AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN2_BASE_ADDR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN2_PIN);

    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN2_PIN, CONFIG_GPIO_STB_MCAN2_DIR);
    /* Instance 5 */
    /* Get address after translation translate */
    baseAddr = (uint32_t) AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN1_BASE_ADDR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN1_PIN);

    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN1_PIN, CONFIG_GPIO_STB_MCAN1_DIR);
    /* Instance 6 */
    /* Get address after translation translate */
    baseAddr = (uint32_t) AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN6_BASE_ADDR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN6_PIN);

    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN6_PIN, CONFIG_GPIO_STB_MCAN6_DIR);
    /* Instance 7 */
    /* Get address after translation translate */
    baseAddr = (uint32_t) AddrTranslateP_getLocalAddr(CONFIG_GPIO_STB_MCAN0_BASE_ADDR);
    GPIO_pinWriteLow(baseAddr, CONFIG_GPIO_STB_MCAN0_PIN);

    GPIO_setDirMode(baseAddr, CONFIG_GPIO_STB_MCAN0_PIN, CONFIG_GPIO_STB_MCAN0_DIR);
}


/* ----------- GPIO Interrupt de-initialization ----------- */
void GPIO_deinit()
{

}


/*
 * MCAN
 */
extern MCAN_BitTimingParams gMcanBitTimingDefaultParams;


/* MCAN Default Bit timing Parameters */
MCAN_BitTimingParams gMcanBitTimingDefaultParams =
{
    .nomRatePrescalar   = 0x3U,
    .nomTimeSeg1        = 0xFU,
    .nomTimeSeg2        = 0x2U,
    .nomSynchJumpWidth  = 0x0U,
    .dataRatePrescalar  = 0x1U,
    .dataTimeSeg1       = 0x5U,
    .dataTimeSeg2       = 0x0U,
    .dataSynchJumpWidth = 0x0U,
};


/*
 * MCU_LBIST
 */

uint32_t gMcuLbistTestStatus = 0U;

void SDL_lbist_selftest(void)
{
}

void Pinmux_init(void);
void PowerClock_init(void);
void PowerClock_deinit(void);
/*
 * Common Functions
 */
void System_init(void)
{
    /* DPL init sets up address transalation unit, on some CPUs this is needed
     * to access SCICLIENT services, hence this needs to happen first
     */
    Dpl_init();

    
    /* initialize PMU */
    CycleCounterP_init(SOC_getSelfCpuClk());

    PowerClock_init();
    /* Now we can do pinmux */
    Pinmux_init();
    /* finally we initialize all peripheral drivers */
    /* IPC Notify */
    {
        IpcNotify_Params notifyParams;
        int32_t status;

        /* initialize parameters to default */
        IpcNotify_Params_init(&notifyParams);

        /* specify the priority of IPC Notify interrupt */
        notifyParams.intrPriority = 15U;

        /* specify the core on which this API is called */
        notifyParams.selfCoreId = CSL_CORE_ID_R5FSS1_1;

        /* list the cores that will do IPC Notify with this core
        * Make sure to NOT list 'self' core in the list below
        */
        notifyParams.numCores = 1;
        notifyParams.coreIdList[0] = CSL_CORE_ID_R5FSS0_0;

        notifyParams.isMailboxIpcEnabled = 0;

        notifyParams.isCrcEnabled = 0;

        notifyParams.isIPCIntrRouter = 0;

        /* initialize the IPC Notify module */
        status = IpcNotify_init(&notifyParams);
        DebugP_assert(status==SystemP_SUCCESS);

    }

    Drivers_uartInit();
    GPIO_init();
}

void System_deinit(void)
{
    IpcNotify_deInit();

    UART_deinit();
    GPIO_deinit();
    PowerClock_deinit();

    Dpl_deinit();
}
