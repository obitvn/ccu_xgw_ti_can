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
 * \file ti_enet_open_close.c
 *
 * \brief This file contains enet driver memory allocation related functionality.
 */




#include <include/per/cpsw.h>
#include <include/core/enet_dma.h>
#include "ti_drivers_config.h"
#include <utils/include/enet_apputils.h>
#include <include/core/enet_rm.h>
#include <utils/include/enet_appsoc.h>
#include "ti_enet_config.h"

extern EnetDma_Cfg gDmaCfg;

const EnetRm_ResCfg gEnetRmResCfg =
{
    .selfCoreId = CSL_CORE_ID_R5FSS0_0,
    .resPartInfo =
    {
        .numCores = 1,
        .coreResInfo =
        {
            [0] =
            {
                .numTxCh       = ENET_SYSCFG_TX_CHANNELS_NUM,
                .numRxCh       = 1,
                .coreId        = CSL_CORE_ID_R5FSS0_0,
                .numRxFlows    = (ENET_SYSCFG_RX_FLOWS_NUM + 1),
                .numMacAddress = 4,
                .numHwPush     = 0,

            },
        },
        .isStaticTxChanAllocated = false,
    },
    .ioctlPermissionInfo =
    {
        .defaultPermittedCoreMask = 0xFFFFFFFFU,
        .numEntries = 0,
        .entry = {{0}}
    },
    .macList =
    {
        .numMacAddress = 0,
        .macAddress =
        {
                [0] = {0, 0, 0, 0, 0, 0},
                [1] = {0, 0, 0, 0, 0, 0},
                [2] = {0, 0, 0, 0, 0, 0},
                [3] = {0, 0, 0, 0, 0, 0},
                [4] = {0, 0, 0, 0, 0, 0},
                [5] = {0, 0, 0, 0, 0, 0},
                [6] = {0, 0, 0, 0, 0, 0},
                [7] = {0, 0, 0, 0, 0, 0},
                [8] = {0, 0, 0, 0, 0, 0},
                [9] = {0, 0, 0, 0, 0, 0},
        }
    },
};

Cpsw_Cfg gEnetCpswCfg =
{
    .escalatePriorityLoadVal = CPSW_ESC_PRI_LD_VAL,
    .dmaCfg = &gDmaCfg,
    .vlanCfg =
    {
        .vlanAware = false,
        .vlanSwitch = ENET_VLAN_TAG_TYPE_INNER,
        .outerVlan = 0x88A8U,
        .innerVlan = 0x8100U,
    },
    .txMtu =
    {
        1536,
        1536,
        1536,
        1536,
        1536,
        1536,
        1536,
        1536
    },
    .hostPortCfg =
    {
        .crcType           = ENET_CRC_ETHERNET,
        .removeCrc         = false,
        .padShortPacket    = true,
        .passCrcErrors     = false,
        .rxMtu             = 1518,
        .passPriorityTaggedUnchanged = false,
        .rxCsumOffloadEn   = true,
        .txCsumOffloadEn   = true,
        .rxVlanRemapEn     = false,
        .rxDscpIPv4RemapEn = true,
        .rxDscpIPv6RemapEn = false,
        .vlanCfg           =
        {
            .portPri = 0,
            .portCfi = false,
            .portVID = 0,
        },
        .rxPriorityType    = ENET_INGRESS_PRI_TYPE_FIXED,
        .txPriorityType    = ENET_EGRESS_PRI_TYPE_FIXED,
    },
    .aleCfg =
    {
        .modeFlags = (CPSW_ALE_CFG_MODULE_EN),
        .policerGlobalCfg =
        {
            .policingEn = false,
            .yellowDropEn = false,
            .redDropEn = false,
            .yellowThresh = CPSW_ALE_POLICER_YELLOWTHRESH_DROP_PERCENT_100,
            .policerNoMatchMode = CPSW_ALE_POLICER_NOMATCH_MODE_GREEN,
            .noMatchPolicer =
            {
                .peakRateInBitsPerSec = 0,
                .commitRateInBitsPerSec = 0
            }
        },
        .agingCfg =
        {
            .autoAgingEn = true,
            .agingPeriodInMs = 1000
        },
        .vlanCfg =
        {
            .aleVlanAwareMode   = true,
            .cpswVlanAwareMode  = false,
            .autoLearnWithVlan  = false,
            .unknownVlanNoLearn = false,
            .unknownForceUntaggedEgressMask = (0),
            .unknownRegMcastFloodMask       = (0 | CPSW_ALE_HOST_PORT_MASK | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_1) | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_2)),
            .unknownUnregMcastFloodMask     = (0 | CPSW_ALE_HOST_PORT_MASK | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_1) | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_2)),
            .unknownVlanMemberListMask      = (0 | CPSW_ALE_HOST_PORT_MASK | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_1) | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_2)),
        },
        .nwSecCfg =
        {
            .hostOuiNoMatchDeny  = false,
            .vid0ModeEn          = true,
            .malformedPktCfg     = {
                                    .srcMcastDropDis = false,
                                    .badLenPktDropEn = false,
                                },
            .ipPktCfg            = {
                                    .dfltNoFragEn          = false,
                                    .dfltNxtHdrWhitelistEn = false,
                                    .ipNxtHdrWhitelistCnt  =  0,
                                    .ipNxtHdrWhitelist     = {
                                                                
                                                                },
                                },


            .macAuthCfg          = {
                                    .authModeEn           = false,
                                    .macAuthDisMask       = (0 | CPSW_ALE_HOST_PORT_MASK),
                                },
        },
        .portCfg =
        {
            [CPSW_ALE_HOST_PORT_NUM] =
            {
                .learningCfg =
                {
                    .noLearn         = false,
                    .noSaUpdateEn    = false,
                },
                .vlanCfg =
                {
                    .vidIngressCheck = false,
                    .dropUntagged    = false,
                    .dropDualVlan    = false,
                    .dropDoubleVlan  = false,
                },
                .macModeCfg =
                {
                    .macOnlyCafEn    = false,
                    .macOnlyEn       = false,
                },
                .pvidCfg =
                {
                    .vlanIdInfo      =
                    {
                        .vlanId   = 0,
                        .tagType  = ENET_VLAN_TAG_TYPE_INNER,
                    },
                    .vlanMemberList          = (0 | CPSW_ALE_HOST_PORT_MASK | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_1) | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_2)),
                    .unregMcastFloodMask     = (0 | CPSW_ALE_HOST_PORT_MASK | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_1) | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_2)),
                    .regMcastFloodMask       = (0 | CPSW_ALE_HOST_PORT_MASK | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_1) | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_2)),
                    .forceUntaggedEgressMask = (0),
                    .noLearnMask             = (0),
                    .vidIngressCheck         = false,
                    .limitIPNxtHdr           = false,
                    .disallowIPFrag          = false,
                },
            },
            [CPSW_ALE_MACPORT_TO_ALEPORT(ENET_MAC_PORT_1)] =
            {
                .learningCfg =
                {
                    .noLearn         = false,
                    .noSaUpdateEn    = false,
                },
                .vlanCfg =
                {
                    .vidIngressCheck = false,
                    .dropUntagged    = false,
                    .dropDualVlan    = false,
                    .dropDoubleVlan  = false,
                },
                .macModeCfg =
                {
                    .macOnlyCafEn    = false,
                    .macOnlyEn       = false,
                },
                .pvidCfg =
                {
                    .vlanIdInfo      =
                    {
                        .vlanId   = 0,
                        .tagType  = ENET_VLAN_TAG_TYPE_INNER,
                    },
                    .vlanMemberList          = (0 | CPSW_ALE_HOST_PORT_MASK | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_1) | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_2)),
                    .unregMcastFloodMask     = (0 | CPSW_ALE_HOST_PORT_MASK | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_1) | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_2)),
                    .regMcastFloodMask       = (0 | CPSW_ALE_HOST_PORT_MASK | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_1) | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_2)),
                    .forceUntaggedEgressMask = (0),
                    .noLearnMask             = (0),
                    .vidIngressCheck         = false,
                    .limitIPNxtHdr           = false,
                    .disallowIPFrag          = false,
                },
            },
            [CPSW_ALE_MACPORT_TO_ALEPORT(ENET_MAC_PORT_2)] =
            {
                .learningCfg =
                {
                    .noLearn         = false,
                    .noSaUpdateEn    = false,
                },
                .vlanCfg =
                {
                    .vidIngressCheck = false,
                    .dropUntagged    = false,
                    .dropDualVlan    = false,
                    .dropDoubleVlan  = false,
                },
                .macModeCfg =
                {
                    .macOnlyCafEn    = false,
                    .macOnlyEn       = false,
                },
                .pvidCfg =
                {
                    .vlanIdInfo      =
                    {
                        .vlanId   = 0,
                        .tagType  = ENET_VLAN_TAG_TYPE_INNER,
                    },
                    .vlanMemberList          = (0 | CPSW_ALE_HOST_PORT_MASK | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_1) | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_2)),
                    .unregMcastFloodMask     = (0 | CPSW_ALE_HOST_PORT_MASK | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_1) | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_2)),
                    .regMcastFloodMask       = (0 | CPSW_ALE_HOST_PORT_MASK | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_1) | CPSW_ALE_MACPORT_TO_PORTMASK(ENET_MAC_PORT_2)),
                    .forceUntaggedEgressMask = (0),
                    .noLearnMask             = (0),
                    .vidIngressCheck         = false,
                    .limitIPNxtHdr           = false,
                    .disallowIPFrag          = false,
                },
            },
        },
        .policerTablePartSize =
        {

        }
    },
    .cptsCfg =
    {
        .hostRxTsEn     = false,
        .tsCompPolarity = true,
        .tsRxEventsDis  = false,
        .tsGenfClrEn    = true,
        .cptsRftClkFreq = CPSW_CPTS_RFTCLK_FREQ_200MHZ,
    },
    .mdioCfg =
    {
        .mode               = MDIO_MODE_STATE_CHANGE_MON,
        .mdioBusFreqHz      = 2200000,
        .phyStatePollFreqHz = 22000,
        .pollEnMask         = -1,
        .c45EnMask          = 0,
        .isMaster           = true,
        .disableStateMachineOnInit = false,
    },
    .resCfg = gEnetRmResCfg,
    .intrPriority = 1U,
    .mdioLinkStateChangeCb = NULL,
    .mdioLinkStateChangeCbArg = NULL,
    .portLinkStatusChangeCb = NULL,
    .portLinkStatusChangeCbArg = NULL,
    .enableQsgmii0RDC = false,
    .enableQsgmii1RDC = false,
    .disablePhyDriver = false,
};

Cpsw_Cfg * EnetApp_getCpswCfg(const Enet_Type enetType, const uint32_t instId)
{
    Cpsw_Cfg * pCpswCfg = NULL;

    if(Enet_isCpswFamily(enetType))
    {
        pCpswCfg = &gEnetCpswCfg;
    }

    return pCpswCfg;
}

void EnetApp_cpswInitMacAddr(const Enet_Type enetType,
                          const uint32_t instId)
{
    int32_t status;
    Cpsw_Cfg * pCpswCfg = NULL;
    EnetRm_ResCfg *resCfg =NULL;
    pCpswCfg = EnetApp_getCpswCfg(enetType, instId);

    EnetAppUtils_assert(pCpswCfg != NULL);
    resCfg = &pCpswCfg->resCfg;
    EnetAppUtils_assert(resCfg != NULL);

    status = EnetAppSoc_getMacAddrList(enetType,
                                       instId,
                                       resCfg->macList.macAddress,
                                       &resCfg->macList.numMacAddress);
    EnetAppUtils_assert(status == ENET_SOK);
    if (resCfg->macList.numMacAddress > ENET_ARRAYSIZE(resCfg->macList.macAddress))
    {
        EnetAppUtils_print("EnetApp_cpswInitMacAddr: "
                           "Limiting number of mac address entries to resCfg->macList.macAddress size"
                           "Available:%u, LimitedTo: %u",
                           resCfg->macList.numMacAddress,
                           ENET_ARRAYSIZE(resCfg->macList.macAddress));
        resCfg->macList.numMacAddress = ENET_ARRAYSIZE(resCfg->macList.macAddress);
    }

    EnetAppUtils_updatemacResPart(&resCfg->resPartInfo,
                                  resCfg->macList.numMacAddress,
                                  resCfg->selfCoreId);
}


