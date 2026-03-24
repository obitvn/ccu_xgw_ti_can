/*
 *  Copyright (c) Texas Instruments Incorporated 2025
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

/*!
 * \file ti_dma_init.c
 *
 * \brief This file contains dma obj initialization required by the app.
 */


#include <include/core/enet_dma.h>
#include <utils/include/enet_apputils.h>
#include "ti_drivers_config.h"
#include "ti_enet_config.h"
#include "ti_enet_dma_init.h"

const EnetApp_DmaCfg g_EnetApp_dmaChParams =
{
    .txChInitCfg = 
    {
        [0] = 
        {
            .txNumPkts    = 16,
        },
    },
    .rxChInitCfg = 
    {
        [0] = 
        {
            .rxNumPkts       = 32,
            .allocMacAddrCnt = 1,
        },
    }
};


EnetDma_Cfg gDmaCfg = 
{
    .enHostRxTsFlag     = false,
    .rxChInitPrms =
    {
        .rxBufferOffset = 0U,
    },
    .maxTxChannels = ENET_SYSCFG_TX_CHANNELS_NUM,
    .maxRxChannels = ENET_SYSCFG_RX_FLOWS_NUM,
};

void EnetApp_updateTxChInitCfg(EnetCpdma_OpenTxChPrms * pTxChPrms, uint32_t chId)
{
    EnetAppUtils_assert(chId < ENET_ARRAYSIZE(g_EnetApp_dmaChParams.txChInitCfg));
    pTxChPrms->numTxPkts = g_EnetApp_dmaChParams.txChInitCfg[chId].txNumPkts;

    return ;
}

void EnetApp_updateRxChInitCfg(EnetCpdma_OpenRxChPrms * pRxChPrms, uint32_t chId)
{
    EnetAppUtils_assert(chId < ENET_ARRAYSIZE(g_EnetApp_dmaChParams.rxChInitCfg));
    pRxChPrms->numRxPkts = g_EnetApp_dmaChParams.rxChInitCfg[chId].rxNumPkts;
    
    return ;
}



