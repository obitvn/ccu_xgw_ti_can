/*
 *  Copyright (C) 2021-2024 Texas Instruments Incorporated
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
#include <drivers/pinmux.h>

static Pinmux_PerCfg_t gPinMuxMainDomainCfg[] = {
                /* GPIO43 -> EPWM0_A (B2) */
    {
        PIN_EPWM0_A,
        ( PIN_MODE(7) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW | PIN_GPIO_R5SS1_1 )
    },
                /* GPIO45 -> EPWM1_A (D3) */
    {
        PIN_EPWM1_A,
        ( PIN_MODE(7) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW | PIN_GPIO_R5SS1_1 )
    },
                /* GPIO62 -> EPWM9_B (J2) */
    {
        PIN_EPWM9_B,
        ( PIN_MODE(7) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW | PIN_GPIO_R5SS1_1 )
    },
                /* GPIO70 -> EPWM13_B (K3) */
    {
        PIN_EPWM13_B,
        ( PIN_MODE(7) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW | PIN_GPIO_R5SS1_1 )
    },
                /* GPIO130 -> EQEP0_A (B14) */
    {
        PIN_EQEP0_A,
        ( PIN_MODE(7) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW | PIN_GPIO_R5SS1_1 )
    },
                /* GPIO83 -> MMC_SDWP (C6) */
    {
        PIN_MMC_SDWP,
        ( PIN_MODE(7) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW | PIN_GPIO_R5SS1_1 )
    },
                /* GPIO44 -> EPWM0_B (B1) */
    {
        PIN_EPWM0_B,
        ( PIN_MODE(7) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW | PIN_GPIO_R5SS1_1 )
    },
                /* GPIO84 -> MMC_SDCD (A5) */
    {
        PIN_MMC_SDCD,
        ( PIN_MODE(7) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW | PIN_GPIO_R5SS1_1 )
    },

            /* MCAN3 pin config */
    /* MCAN3_RX -> UART0_CTSn (B7) */
    {
        PIN_UART0_CTSN,
        ( PIN_MODE(3) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
    /* MCAN3 pin config */
    /* MCAN3_TX -> UART0_RTSn (C7) */
    {
        PIN_UART0_RTSN,
        ( PIN_MODE(3) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
            /* MCAN2 pin config */
    /* MCAN2_RX -> MCAN2_RX (A12) */
    {
        PIN_MCAN2_RX,
        ( PIN_MODE(0) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
    /* MCAN2 pin config */
    /* MCAN2_TX -> MCAN2_TX (B12) */
    {
        PIN_MCAN2_TX,
        ( PIN_MODE(0) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
            /* MCAN7 pin config */
    /* MCAN7_RX -> EPWM12_A (K2) */
    {
        PIN_EPWM12_A,
        ( PIN_MODE(4) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
    /* MCAN7 pin config */
    /* MCAN7_TX -> EPWM12_B (J4) */
    {
        PIN_EPWM12_B,
        ( PIN_MODE(4) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
            /* MCAN6 pin config */
    /* MCAN6_RX -> EPWM14_A (V17) */
    {
        PIN_EPWM14_A,
        ( PIN_MODE(3) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
    /* MCAN6 pin config */
    /* MCAN6_TX -> EPWM14_B (T16) */
    {
        PIN_EPWM14_B,
        ( PIN_MODE(3) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
            /* MCAN0 pin config */
    /* MCAN0_RX -> MMC_CLK (B6) */
    {
        PIN_MMC_CLK,
        ( PIN_MODE(3) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
    /* MCAN0 pin config */
    /* MCAN0_TX -> MMC_CMD (A4) */
    {
        PIN_MMC_CMD,
        ( PIN_MODE(3) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
            /* MCAN1 pin config */
    /* MCAN1_RX -> MMC_DAT0 (B5) */
    {
        PIN_MMC_DAT0,
        ( PIN_MODE(3) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
    /* MCAN1 pin config */
    /* MCAN1_TX -> MMC_DAT1 (B4) */
    {
        PIN_MMC_DAT1,
        ( PIN_MODE(3) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
            /* MCAN4 pin config */
    /* MCAN4_RX -> MMC_DAT2 (A3) */
    {
        PIN_MMC_DAT2,
        ( PIN_MODE(3) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
    /* MCAN4 pin config */
    /* MCAN4_TX -> MMC_DAT3 (A2) */
    {
        PIN_MMC_DAT3,
        ( PIN_MODE(3) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
            /* MCAN5 pin config */
    /* MCAN5_RX -> EPWM10_A (G4) */
    {
        PIN_EPWM10_A,
        ( PIN_MODE(4) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
    /* MCAN5 pin config */
    /* MCAN5_TX -> EPWM10_B (J3) */
    {
        PIN_EPWM10_B,
        ( PIN_MODE(4) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },

            /* UART5 pin config */
    /* UART5_RXD -> SDFM0_CLK3 (A15) */
    {
        PIN_SDFM0_CLK3,
        ( PIN_MODE(1) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },
    /* UART5 pin config */
    /* UART5_TXD -> SDFM0_CLK2 (B15) */
    {
        PIN_SDFM0_CLK2,
        ( PIN_MODE(0) | PIN_PULL_DISABLE | PIN_SLEW_RATE_LOW )
    },

    {PINMUX_END, PINMUX_END}
};


/*
 * Pinmux
 */


void Pinmux_init(void)
{



    Pinmux_config(gPinMuxMainDomainCfg, PINMUX_DOMAIN_ID_MAIN);
    
}


