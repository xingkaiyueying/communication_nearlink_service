/*
 * Copyright (C) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "qosm_trans_channel.h"

#include "securec.h"

#include "cm_api.h"
#include "cm_def.h"
#include "cm_errno.h"
#include "cm_dyn_trans_channel_api.h"
#include "cm_logic_link_api.h"
#include "cm_signaling_internal.h"
#include "common_ext_func_wrapper.h"
#include "cp_errno_base.h"
#include "cp_worker.h"
#include "qosm_errno.h"
#include "qosm_log.h"
#include "sdf_addr.h"
#include "sdf_dlist.h"
#include "sdf_map.h"
#include "sdf_mem.h"
#include "sle_logic_link_mgr.h"

#define QOSM_TC_CRC16_INIT 0x5555      // 传输通道crc生成种子，规范默认0x5555
#define QOSM_TC_TX_WINDOW 50          // 可靠模式传输通道默认发送队列的长度，单位PDU个数
#define QOSM_TC_REORDER_TIMEOUT 10000  // 可靠模式传输通道默认重排序定时器超时时间，单位ms
#define QOSM_TC_RETRANS_TIMEOUT 1000   // 可靠模式传输通道默认重传定时器超时时间，单位ms
#define QOSM_TC_RSP_TIMEOUT 1000       // 可靠模式传输通道默认应答定时器超时时间，单位ms
#define QOSM_TC_MAX_TX_CNT 5           // 可靠模式传输通道默认最大传输次数
#define QOSM_FRAME_4_TC_MAX_TX_CNT 22  // 使用帧四时，可靠模式传输通道默认最大传输次数
#define QOSM_TC_DEFAULT_MPS 1500       // 可靠模式传输通道默认mps

#define QOSM_SLE_CONN_NORMAL_SPEED_INTERAL 0x64      // 链路调度最小间隔 100 * 0.125 = 12.5ms
#define QOSM_SLE_CONN_HIGH_SPEED_INTERAL 0x24        // 链路调度最小间隔 36 * 0.125ms = 4.5 ms
#define QOSM_SLE_CONN_HIGH_SPEED_INTERAL_EXT 0x27  // 链路调度最小间隔 39 * 0.125ms = 4.875 ms

#define QOSM_SLE_CONN_EVENT_IFS 125             // 链路调度间隔, 125us
#define QOSM_SLE_CONN_TIME_UNIT 4               // 系统调度时隙, 125us
#define QOSM_SLE_CONN_SUPERVISION_TIMEOUT 1000  // 超时时间10s

#define QOSM_CHANNEL_MAX_SIZE 20       // 最大传输通道数

enum {
    QOSM_SLE_PHY_TYPE_1M = 0x0,  // 1M PHY
    QOSM_SLE_PHY_TYPE_4M = 0x2,  // 4M PHY
};

enum {
    QOSM_SLE_PILOT_DENSITY_4_TO_1 = 0x0,   // 导频密度为4:1
    QOSM_SLE_PILOT_DENSITY_16_TO_1 = 0x2,  // 导频密度为16:1
};

typedef struct {
    uint16_t lcid;
    uint8_t role;
    SLE_Addr_S addr;
    QOSM_TransChannelSlqi_E slqi;
    SDF_DListHead_S list;
} QOSM_LogicLink_S;

typedef struct {
    uint16_t interval;
} QOSM_SleConnParams_S;

typedef struct {
    uint8_t format;
    uint8_t phy;
    uint8_t pilotDensity;
} QOSM_SlePhyParams_S;

typedef struct {
    QOSM_SleConnParams_S connParams;
    QOSM_SlePhyParams_S phyParams;
} QOSM_SleLogicLinkParams_S;

typedef struct {
    SDF_DListEntry_S entry;
    uint8_t tcid;
    uint16_t srcPort;
    uint16_t dstPort;
    QOSM_TransChannelSlqi_E slqi;
} QOSM_TransChannel_S;

typedef struct {
    SDF_DListEntry_S entry;
    uint16_t lcid;
    uint16_t channelNum;
} QOSM_TransChannelNumNode;

typedef struct {
    SDF_DListHead_S channelNumHead;
    uint32_t channelTotalNum;
} QOSM_TransChannelNum;

static QOSM_TransChannelNum g_transChannelNum = {
    .channelNumHead = {{&(g_transChannelNum.channelNumHead.list), &(g_transChannelNum.channelNumHead.list)}, 0},
    .channelTotalNum = 0,
};

static SDF_Map *g_transChannelMap = NULL;  // key: lcid, value: list of QOSM_TransChannel_S *

static QOSM_TransChannelCbks_S g_transChannelCbks = { 0 };
static QOSM_IpSharePeerCheck g_ipSharePeerCheck;

static void QOSM_RegisterIpSharePeerCheckProc(void *arg)
{
    g_ipSharePeerCheck = *(QOSM_IpSharePeerCheck *)arg;
}

uint32_t QOSM_RegisterIpSharePeerCheck(QOSM_IpSharePeerCheck callback)
{
    QOSM_IpSharePeerCheck *copy = SDF_MemAlloc(sizeof(*copy));
    QOSM_CHECK_RETURN_RET(copy != NULL, QOSM_MALLOC_ERR, "malloc failed.");
    *copy = callback;
    uint32_t ret = CP_PostTask(QOSM_RegisterIpSharePeerCheckProc, copy, SDF_MemFree);
    return ret == CP_OK ? QOSM_SUCCESS : QOSM_POST_TASK_ERR;
}

static QOSM_SleLogicLinkParams_S g_sleLogicLinkParams[QOSM_TRANS_CHANNEL_SLQI_MAX] = {
    {
        { QOSM_SLE_CONN_NORMAL_SPEED_INTERAL },
        { QOSM_SLE_RADIO_FRAME_TYPE_2, QOSM_SLE_PHY_TYPE_1M, QOSM_SLE_PILOT_DENSITY_16_TO_1 },
    },
    {
        { QOSM_SLE_CONN_HIGH_SPEED_INTERAL },
        { QOSM_SLE_RADIO_FRAME_TYPE_2, QOSM_SLE_PHY_TYPE_4M, QOSM_SLE_PILOT_DENSITY_4_TO_1 },
    },
};

static inline int QOSM_LogicLinkCompare(const void *lhs_, const void *rhs_)
{
    uint16_t lhs = *(uint16_t *)lhs_;
    uint16_t rhs = *(uint16_t *)rhs_;
    return lhs - rhs;
}

static void QOSM_DListNodeFree(SDF_DListEntry_S *entry)
{
    if (entry == NULL) {
        return;
    }
    SDF_MemFree(entry);
}

static void QOSM_LogicLinksDtor(void *args)
{
    QOSM_CHECK_RETURN(args != NULL, "args is null");
    QOSM_LogicLink_S *logicLink = (QOSM_LogicLink_S *)args;

    SDF_DListDestroy(&logicLink->list, QOSM_DListNodeFree);
    SDF_MemFree(logicLink);
}

static void QOSM_ChannelDoNothing(void *arg) {}
static uint32_t QOSM_TransChannelMapInit(void)
{
    SDF_Traits keyTraits = {
        .dtor = QOSM_ChannelDoNothing,
        .cmptor = QOSM_LogicLinkCompare,
    };
    SDF_Traits valTraits = {
        .dtor = QOSM_LogicLinksDtor,
        .cmptor = NULL,
    };

    g_transChannelMap = SDF_MapCtor(keyTraits, valTraits);
    QOSM_CHECK_RETURN_RET(g_transChannelMap != NULL, QOSM_MAP_CREATE_ERR, "malloc failed.");
    return QOSM_SUCCESS;
}

static void QOSM_TransChannelMapDeInit(void)
{
    if (g_transChannelMap != NULL) {
        SDF_MapDtor(g_transChannelMap);
        g_transChannelMap = NULL;
    }
}

static bool QOSM_IsChannelExceedMaxSize(void)
{
    QOSM_LOGD("trans channel total num: %u", g_transChannelNum.channelTotalNum);
    return g_transChannelNum.channelTotalNum >= QOSM_CHANNEL_MAX_SIZE;
}

static bool QOSM_IsChannelEmpty(void)
{
    QOSM_LOGD("trans channel total num: %u", g_transChannelNum.channelTotalNum);
    return g_transChannelNum.channelTotalNum == 0;
}

static void QOSM_IncreaseChannelSize(uint16_t lcid)
{
    QOSM_TransChannelNumNode *node = NULL;
    SDF_DListElmForeach(node, &g_transChannelNum.channelNumHead, entry) {
        if (node->lcid == lcid) {
            node->channelNum++;
            g_transChannelNum.channelTotalNum++;
            QOSM_LOGD("trans channel total num: %u", g_transChannelNum.channelTotalNum);
            return;
        }
    }
    node = (QOSM_TransChannelNumNode *)SDF_MemZalloc(sizeof(QOSM_TransChannelNumNode));
    QOSM_CHECK_RETURN(node != NULL, "malloc node failed.");
    SDF_DListEntryInit(&node->entry);
    node->lcid = lcid;
    node->channelNum = 1;
    SDF_DListElmTailInsert(&g_transChannelNum.channelNumHead, node, entry);
    g_transChannelNum.channelTotalNum++;
    QOSM_LOGD("trans channel total num: %u", g_transChannelNum.channelTotalNum);
}

static void QOSM_DecreaseChannelSize(uint16_t lcid)
{
    QOSM_TransChannelNumNode *node = NULL;
    QOSM_TransChannelNumNode *temp = NULL;
    SDF_DListElmSafeForeach(node, temp, &g_transChannelNum.channelNumHead, entry) {
        if (node->lcid == lcid) {
            if (node->channelNum > 0) {
                node->channelNum--;
                g_transChannelNum.channelTotalNum--;
            }
            if (node->channelNum == 0) {
                SDF_DListElmDel(&g_transChannelNum.channelNumHead, node, entry);
                SDF_MemFree(node);
            }
            QOSM_LOGD("trans channel total num: %u", g_transChannelNum.channelTotalNum);
            return;
        }
    }
    QOSM_LOGD("lcid: %u not found", lcid);
}

static void QOSM_ClearChannelSize(uint16_t lcid)
{
    QOSM_TransChannelNumNode *node = NULL;
    QOSM_TransChannelNumNode *temp = NULL;
    SDF_DListElmSafeForeach(node, temp, &g_transChannelNum.channelNumHead, entry) {
        if (node->lcid == lcid) {
            g_transChannelNum.channelTotalNum  = g_transChannelNum.channelTotalNum > node->channelNum ?
                g_transChannelNum.channelTotalNum - node->channelNum : 0;
            SDF_DListElmDel(&g_transChannelNum.channelNumHead, node, entry);
            SDF_MemFree(node);
            QOSM_LOGD("trans channel total num: %u", g_transChannelNum.channelTotalNum);
            return;
        }
    }
    QOSM_LOGD("lcid: %u not found", lcid);
}

static QOSM_LogicLink_S *QOSM_LogicLinkFind(uint16_t lcid)
{
    SDF_MapIter *iter = SDF_MapFind(g_transChannelMap, &lcid);
    if (iter != NULL) {
        return iter->val;
    }
    return NULL;
}

static void QOSM_LogicLinkAdd(CM_LogicLinkState_S *state)
{
    uint16_t lcid = state->lcid;
    QOSM_LOGI("add logic link, lcid: %hu.", lcid);
    QOSM_LogicLink_S *logicLink = QOSM_LogicLinkFind(lcid);
    QOSM_CHECK_RETURN(logicLink == NULL, "logic link exists, lcid: %hu.", lcid);

    logicLink = (QOSM_LogicLink_S *)SDF_MemZalloc(sizeof(QOSM_LogicLink_S));
    QOSM_CHECK_RETURN(logicLink != NULL, "malloc failed.");

    logicLink->lcid = lcid;
    logicLink->role = state->role;
    logicLink->slqi = QOSM_TRANS_CHANNEL_SLQI_LOW;
    (void)memcpy_s(&logicLink->addr, sizeof(SLE_Addr_S), &state->addr, sizeof(SLE_Addr_S));
    SDF_DListHeadInit(&logicLink->list);

    if (!SDF_MapMoveInsert(g_transChannelMap, &logicLink->lcid, logicLink)) {
        QOSM_LOGE("insert dtap logic link to map failed");
        SDF_MemFree(logicLink);
        return;
    }
}

static void QOSM_LogicLinkDel(const CM_LogicLinkState_S *state)
{
    uint16_t lcid = state->lcid;
    QOSM_LOGI("del logic link, lcid: %hu.", lcid);
    (void)SDF_MapErase(g_transChannelMap, &lcid);
    QOSM_ClearChannelSize(lcid);
}

static void QOSM_LogicLinkCbk(CM_LogicLinkState_S *state)
{
    QOSM_CHECK_RETURN(state != NULL, "state is null.");
    if (state->result == CM_LINK_STATE_CONNECTED) {
        QOSM_LogicLinkAdd(state);
    } else if (state->result == CM_LINK_STATE_DISCONNECTED) {
        QOSM_LogicLinkDel(state);
    }
}

static uint32_t QOSM_LogicLinkCbksReg(void)
{
    CM_LogicLinkCbks_S cbks = {
        .moduleId = CM_MODULE_QOSM,
        .logicLinkCbk = QOSM_LogicLinkCbk,
        .remoteFeaturesCbk = NULL,
        .connUpdateParamCbk = NULL,
    };

    return CM_RegLogicLinkListener(&cbks);
}

static void QOSM_LogicLinkCbksUnreg(void)
{
    CM_UnRegLogicLinkListener(CM_MODULE_QOSM);
}

static void QOSM_TransChannelCbksRegisterProc(void *args)
{
    QOSM_CHECK_RETURN(args != NULL, "args is NULL.");

    QOSM_TransChannelCbks_S *cbks = (QOSM_TransChannelCbks_S *)args;
    g_transChannelCbks = *cbks;
}

uint32_t QOSM_TransChannelCbksRegister(const QOSM_TransChannelCbks_S *cbks)
{
    QOSM_CHECK_RETURN_RET(cbks != NULL && cbks->statusCbk != NULL, QOSM_NULL_PTR_ERR, "cbks is NULL.");

    QOSM_TransChannelCbks_S *cbksParams = SDF_MemZalloc(sizeof(QOSM_TransChannelCbks_S));
    QOSM_CHECK_RETURN_RET(cbksParams != NULL, QOSM_MALLOC_ERR, "malloc failed.");
    (void)memcpy_s(cbksParams, sizeof(QOSM_TransChannelCbks_S), cbks, sizeof(QOSM_TransChannelCbks_S));

    uint32_t ret = CP_PostTask(QOSM_TransChannelCbksRegisterProc, cbksParams, SDF_MemFree);
    QOSM_CHECK_RETURN_RET(ret == CP_OK, QOSM_POST_TASK_ERR, "post task failed.");
    return QOSM_SUCCESS;
}

static void QOSM_TransChannelCbksUnregisterProc(void *args)
{
    (void)args;

    QOSM_TransChannelCbks_S cbks = { 0 };
    g_transChannelCbks = cbks;
}

uint32_t QOSM_TransChannelCbksUnregister(void)
{
    uint32_t ret = CP_PostTask(QOSM_TransChannelCbksUnregisterProc, NULL, NULL);
    QOSM_CHECK_RETURN_RET(ret == CP_OK, QOSM_POST_TASK_ERR, "post task failed.");
    return QOSM_SUCCESS;
}

static uint32_t QOSM_TransChannelUpdateConnParamsInner(QOSM_LogicLink_S *logicLink, QOSM_TransChannelSlqi_E slqi)
{
    QOSM_LOGI("start to update logic link params, lcid: %hu, slqi: %hhu.", logicLink->lcid, slqi);
    QOSM_CHECK_RETURN_RET(slqi < QOSM_TRANS_CHANNEL_SLQI_MAX, QOSM_INVALID_SLQI_ERR, "invalid slqi.");
    QOSM_SleLogicLinkParams_S *linkParams = &g_sleLogicLinkParams[slqi];

    CM_ConnectUpdateParamReq_S connUpdateReq = {
        .version = 0,
        .localIndex = 0,
        .intervalMin = linkParams->connParams.interval,
        .intervalMax = linkParams->connParams.interval,
        .txRxInterval = QOSM_SLE_CONN_EVENT_IFS,
        .eventInterval = QOSM_SLE_CONN_EVENT_IFS,
        .maxLatency = 0,
        .supervisionTimeout = QOSM_SLE_CONN_SUPERVISION_TIMEOUT,
        .systemTimeUnit = QOSM_SLE_CONN_TIME_UNIT,
        .txRxFlag = 0,
    };
    (void)memcpy_s(&connUpdateReq.addr, sizeof(SLE_Addr_S), &logicLink->addr, sizeof(SLE_Addr_S));
    uint32_t ret = CM_ConnectUpdateParamReq(&connUpdateReq);
    QOSM_CHECK_RETURN_RET(ret == CM_SUCCESS, ret, "update conn params failed, ret: %08x.", ret);

    CM_SetPhyReq_S setPhyReq = {
        .lcid = logicLink->lcid,
        .txFormat = linkParams->phyParams.format,
        .rxFormat = linkParams->phyParams.format,
        .txPhy = linkParams->phyParams.phy,
        .rxPhy = linkParams->phyParams.phy,
        .txPilotDensity = linkParams->phyParams.pilotDensity,
        .rxPilotDensity = linkParams->phyParams.pilotDensity,
        .gFeedback = 0,
        .tFeedback = 0,
    };
    ret = CM_SetPhy(&setPhyReq);
    QOSM_CHECK_RETURN_RET(ret == CM_SUCCESS, ret, "update phy failed, ret: %08x.", ret);
    return QOSM_SUCCESS;
}

static void QOSM_TransChannelUpdateConnParams(uint16_t lcid)
{
    QOSM_TransChannelSlqi_E slqi = QOSM_TRANS_CHANNEL_SLQI_LOW;
    QOSM_TransChannel_S *channel = NULL;
    QOSM_LogicLink_S *logicLink = QOSM_LogicLinkFind(lcid);
    QOSM_CHECK_RETURN(logicLink != NULL, "logic link does not exist");
    SDF_DListHead_S *head = &logicLink->list;

    SDF_DListElmForeach(channel, head, entry) {
        if (channel->slqi == QOSM_TRANS_CHANNEL_SLQI_MAX) {
            continue;
        }

        if (channel->slqi > slqi) {
            slqi = channel->slqi;
        }
    }

    if (logicLink->slqi == slqi) {
        return;
    }

    if (QOSM_TransChannelUpdateConnParamsInner(logicLink, slqi) != CM_SUCCESS) {
        return;
    }

    logicLink->slqi = slqi;
}

static uint32_t QOSM_TransChannelAdd(uint16_t lcid, const QOSM_TransChannelRspParams_S *rsp)
{
    QOSM_LogicLink_S *logicLink = QOSM_LogicLinkFind(lcid);
    QOSM_CHECK_RETURN_RET(logicLink != NULL, QOSM_LOGIC_LINK_FOUND_ERR, "logic link does not exist");

    QOSM_TransChannel_S *channel = (QOSM_TransChannel_S *)SDF_MemZalloc(sizeof(QOSM_TransChannel_S));
    QOSM_CHECK_RETURN_RET(channel != NULL, QOSM_MALLOC_ERR, "malloc failed.");

    SDF_DListEntryInit(&channel->entry);
    channel->tcid = rsp->tcid;
    channel->srcPort = rsp->srcPort;
    channel->dstPort = rsp->dstPort;
    channel->slqi = rsp->slqi;
    SDF_DListElmHeadInsert(&logicLink->list, channel, entry);
    return QOSM_SUCCESS;
}

static uint32_t QOSM_TransChannelDel(uint16_t lcid, const QOSM_TransChannelRspParams_S *rsp)
{
    QOSM_LogicLink_S *logicLink = QOSM_LogicLinkFind(lcid);
    QOSM_CHECK_RETURN_RET(logicLink != NULL, QOSM_LOGIC_LINK_FOUND_ERR, "logic link does not exist");

    QOSM_TransChannel_S *channel = NULL;
    QOSM_TransChannel_S *tmp = NULL;
    SDF_DListElmSafeForeach(channel, tmp, &logicLink->list, entry) {
        if (channel->tcid != rsp->tcid) {
            continue;
        }
        SDF_DListElmDel(&logicLink->list, channel, entry);
        SDF_MemFree(channel);
        break;
    }
    return QOSM_SUCCESS;
}

static uint16_t QOSM_TransChannelMtuNegotiate(uint16_t localMtu, uint16_t peerMtu)
{
    if (peerMtu < CM_CAP_MIN_MTU) {
        return localMtu;
    }
    return peerMtu < localMtu ? peerMtu : localMtu;
}

static void QOSM_TransChannelReliableModeConfigSet(CM_TransModeConfig_S *dstConfig,
                                                   QOSM_TransChannelParams_S *srcParam, CM_CapInfo_S *capInfo)
{
    dstConfig->reliableMode.reorderTimeout = QOSM_TC_REORDER_TIMEOUT;
    dstConfig->reliableMode.crcInit = QOSM_TC_CRC16_INIT;
    dstConfig->reliableMode.retransTimeout = QOSM_TC_RETRANS_TIMEOUT;
    dstConfig->reliableMode.rspTimeout = QOSM_TC_RSP_TIMEOUT;
    if (capInfo->rxWnd != 0) {
        dstConfig->reliableMode.txWindow = (capInfo->rxWnd < QOSM_TC_TX_WINDOW) ? capInfo->rxWnd
                                                                                : QOSM_TC_TX_WINDOW;
    } else {
        QOSM_LOGW("fail to get peer rx window, use default tx window");
        dstConfig->reliableMode.txWindow = QOSM_TC_TX_WINDOW;
    }
    if (srcParam->frameType == QOSM_SLE_RADIO_FRAME_TYPE_4) {
        dstConfig->reliableMode.maxTxThreshold = QOSM_FRAME_4_TC_MAX_TX_CNT;
    } else {
        dstConfig->reliableMode.maxTxThreshold = QOSM_TC_MAX_TX_CNT;
    }
    if (srcParam->linkMode == SLE_MODE_ACB) {
        // 当前采用默认的MPS值，后续调整到链路协商值
        dstConfig->mps = QOSM_TC_DEFAULT_MPS;
        dstConfig->mtu = QOSM_TransChannelMtuNegotiate(CM_CAP_MTU, capInfo->mtu);
    }
}

static uint32_t QOSM_TransChannelModeConfigSet(CM_TransModeConfig_S *dstConfig, QOSM_TransChannelParams_S *srcParam)
{
    QOSM_TransChannelConf_S *srcConfig = &srcParam->tcConf;
    CM_CapInfo_S capInfo = {0};
    if (CM_GetLogicLinkCapInfo(&capInfo, &srcParam->addr) != CM_SUCCESS) {
        QOSM_LOGE("fail to get logic link cap info");
        return QOSM_GET_CAP_INFO_ERR;
    }

    if (capInfo.mtu != 0 && capInfo.mtu < CM_CAP_MIN_MTU) {
        QOSM_LOGW("invalid mtu %hu of peer device %s", capInfo.mtu, GET_ENC_ADDR(&srcParam->addr));
        return QOSM_INVALID_MTU_ERR;
    }

    memset_s(dstConfig, sizeof(CM_TransModeConfig_S), 0, sizeof(CM_TransModeConfig_S));
    dstConfig->transMode = srcConfig->mode;
    dstConfig->mtu = DTAP_MAX_PAYLOAD_LEN;
    dstConfig->mps = UINT16_MAX;

    // 对端不支持指定的传输模式则使用基础模式
    switch (dstConfig->transMode) {
        case CM_TRANS_MODE_RELIABLE:
            if ((capInfo.supportTransMode & RELIABLE_MODE_MASK) != 0) {
                QOSM_TransChannelReliableModeConfigSet(dstConfig, srcParam, &capInfo);
            } else {
                QOSM_LOGW("reliable mode does not support, use basic mode");
                dstConfig->transMode = CM_TRANS_MODE_BASIC;
            }
            break;
        case CM_TRANS_MODE_STREAM:
            if ((capInfo.supportTransMode & STREAM_MODE_MASK) == 0) {
                QOSM_LOGW("stream mode does not support, use basic mode");
                dstConfig->transMode = CM_TRANS_MODE_BASIC;
            }
            break;
        default:
            break;
    }
    return QOSM_SUCCESS;
}

static void QOSM_TransChannelCreateCbk(void *args)
{
    QOSM_CHECK_RETURN(args != NULL, "args is NULL.");

    QOSM_TransChannelParams_S *params = (QOSM_TransChannelParams_S *)args;

    if (QOSM_IsChannelExceedMaxSize()) {
        QOSM_LOGE("tranport channel size exceed max");
        goto FAILURE;
    }

    CM_DynTransChannelEstablishParamReq_S req = {
        .version = 0,
        .localIndex = 0,
        .srcPort = params->srcPort,
        .dstPort = params->dstPort,
        .slqi = params->slqi,
        .extension = NULL,
        .expectedTransportMode = params->accessTransMode,
        .frameType = (CM_TransConnFrameType_E)params->frameType,
    };
    (void)memcpy_s(&req.addr, sizeof(SLE_Addr_S), &params->addr, sizeof(SLE_Addr_S));

    uint32_t ret = QOSM_TransChannelModeConfigSet(&req.transModeConfig, params);
    if (ret != QOSM_SUCCESS) {
        QOSM_LOGE("set mode config failed, ret: 0x%08x", ret);
        goto FAILURE;
    }

    QOSM_LOGI("start to create tranport channel, addr: %s, src port: %hu, dst port: %hu, trans mode: %hhu, "
              "mps: %hu, mtu: %hu, slqi: %hhu, access trans mode: %hhu, connFrameType: %hhu",
              GET_ENC_ADDR(&req.addr), req.srcPort, req.dstPort, req.transModeConfig.transMode,
              req.transModeConfig.mps, req.transModeConfig.mtu, req.slqi, req.expectedTransportMode, req.frameType);
    ret = CM_DynTransChannelEstablishReq(&req);
    if (ret == CM_SUCCESS) {
        SleLogicLink_S* link = SleLogicLinkGetByAddr(&req.addr);
        if (link != NULL) {
            QOSM_IncreaseChannelSize(link->lcid);
        }
        return;
    }
    QOSM_LOGE("create tranport channel failed, ret: 0x%08x", ret);

FAILURE:
    if (g_transChannelCbks.statusCbk != NULL) {
        QOSM_TransChannelRspParams_S rsp = {
            .srcPort = params->srcPort,
            .dstPort = params->dstPort,
            .slqi = params->slqi,
            .status = QOSM_TRANS_CHANNEL_ESTABLISH_FAIL,
        };
        (void)memcpy_s(&rsp.addr, sizeof(SLE_Addr_S), &params->addr, sizeof(SLE_Addr_S));
        g_transChannelCbks.statusCbk(&rsp);
    }
}

static bool QOSM_TransChannelParamsCheck(const QOSM_TransChannelParams_S *params)
{
    QOSM_CHECK_RETURN_RET(params != NULL, false, "param is NULL.");
    QOSM_CHECK_RETURN_RET(params->linkMode == SLE_MODE_ACB, false, "invalid link mode: %hhu.", params->linkMode);
    QOSM_CHECK_RETURN_RET(params->accessTransMode == ACCESS_TRANS_MODE_UNICAST, false,
                          "invalid access transport mode: %hhu.", params->accessTransMode);
    QOSM_CHECK_RETURN_RET(params->slqi < QOSM_TRANS_CHANNEL_SLQI_MAX, false,
                          "invalid slqi: %hhu of qos policy.", params->slqi);
    QOSM_CHECK_RETURN_RET(params->tcConf.mode < TRANSPORT_MODE_MAX &&
                          params->tcConf.mode != TRANSPORT_MODE_TRANSPARENT, false,
                          "invalid transport mode: %hhu.", params->tcConf.mode);
    return true;
}

uint32_t QOSM_TransChannelCreate(const QOSM_TransChannelParams_S *params)
{
    QOSM_CHECK_RETURN_RET(params != NULL, QOSM_NULL_PTR_ERR, "params is NULL.");
    QOSM_CHECK_RETURN_RET(QOSM_TransChannelParamsCheck(params), QOSM_INVALID_PARAM_ERR,
                          "params check failed.");
    QOSM_TransChannelParams_S *args = SDF_MemZalloc(sizeof(QOSM_TransChannelParams_S));
    QOSM_CHECK_RETURN_RET(args != NULL, QOSM_MALLOC_ERR, "malloc failed.");
    (void)memcpy_s(args, sizeof(QOSM_TransChannelParams_S), params, sizeof(QOSM_TransChannelParams_S));

    QOSM_LOGD("QOSM_TransChannelCreate params.frameType: %hhu", params->frameType);
    uint32_t ret = CP_PostTask(QOSM_TransChannelCreateCbk, args, SDF_MemFree);
    QOSM_CHECK_RETURN_RET(ret == CP_OK, QOSM_POST_TASK_ERR, "post task failed.");

    return QOSM_SUCCESS;
}

static void QOSM_TransChannelDestroyCbk(void *args)
{
    QOSM_CHECK_RETURN(args != NULL, "args is NULL.");
    if (QOSM_IsChannelEmpty()) {
        QOSM_LOGI("channel is empty.");
        goto FAILED;
    }
    QOSM_TransChannelReleaseParams_S *params = (QOSM_TransChannelReleaseParams_S *)args;
    CM_DynTransChannelReleaseParamReq_S req = {
        .version = 0,
        .localIndex = 0,
        .srcTcid = params->tcid,
        .dstTcid = 0,
    };
    (void)memcpy_s(&req.addr, sizeof(SLE_Addr_S), &params->addr, sizeof(SLE_Addr_S));
    QOSM_LOGI("start to destroy tranport channel, addr: %s, src tcid: %hhu", GET_ENC_ADDR(&req.addr), req.srcTcid);
    uint32_t ret = CM_DynTransChannelReleaseReq(&req);
    if (ret == CM_SUCCESS) {
        SleLogicLink_S* link = SleLogicLinkGetByAddr(&req.addr);
        if (link != NULL) {
            QOSM_DecreaseChannelSize(link->lcid);
        }
        return;
    }
    QOSM_LOGE("destroy tranport channel failed, ret: 0x%8x", ret);

FAILED:
    if (g_transChannelCbks.statusCbk != NULL) {
        QOSM_TransChannelRspParams_S rsp = {
            .tcid = params->tcid,
            .status = QOSM_TRANS_CHANNEL_RELEASE_FAIL,
        };
        (void)memcpy_s(&rsp.addr, sizeof(SLE_Addr_S), &params->addr, sizeof(SLE_Addr_S));
        g_transChannelCbks.statusCbk(&rsp);
    }
}

uint32_t QOSM_TransChannelDestroy(const QOSM_TransChannelReleaseParams_S *params)
{
    QOSM_CHECK_RETURN_RET(params != NULL, QOSM_NULL_PTR_ERR, "params is NULL.");

    QOSM_TransChannelReleaseParams_S *args = SDF_MemZalloc(sizeof(QOSM_TransChannelReleaseParams_S));
    QOSM_CHECK_RETURN_RET(args != NULL, QOSM_MALLOC_ERR, "malloc failed.");
    (void)memcpy_s(args, sizeof(QOSM_TransChannelReleaseParams_S), params, sizeof(QOSM_TransChannelReleaseParams_S));

    uint32_t ret = CP_PostTask(QOSM_TransChannelDestroyCbk, args, SDF_MemFree);
    QOSM_CHECK_RETURN_RET(ret == CP_OK, QOSM_POST_TASK_ERR, "post task failed.");
    return QOSM_SUCCESS;
}

static void QOSM_TransChannelEstablishRspCbk(const CM_DynTransChanEstablishParamRsp_S *param)
{
    QOSM_CHECK_RETURN(param != NULL, "params is NULL.");

    QOSM_TransChannelRspParams_S rsp = {
        .transMode = param->transMode,
        .lcid = param->lcid,
        .tcid = param->srcTcid,
        .srcPort = param->srcPort,
        .dstPort = param->dstPort,
        // 兼容耳机场景（耳机不支持分片），QOSM_TransChannelMtuNegotiate协商时保证mtu值不小于CM_CAP_MIN_MTU
        .mtu = param->mtu > CM_CAP_MIN_MTU ? (param->mtu - CM_CAP_MIN_MTU) : 0,
        .slqi = QOSM_TRANS_CHANNEL_SLQI_MAX,
        .frameType = (QOSM_TransConnFrameType_E)param->frameType,
    };
    if (param->slqiList.slqiNum != 0) {
        rsp.slqi = param->slqiList.slqi[0];
    }
    (void)memcpy_s(&rsp.addr, sizeof(SLE_Addr_S), &param->addr, sizeof(SLE_Addr_S));
    rsp.status = param->result == CM_DYN_TRANS_CHAN_ESTABLISH_SUCCESS ? QOSM_TRANS_CHANNEL_ESTABLISHED :
                                                                        QOSM_TRANS_CHANNEL_ESTABLISH_FAIL;
    QOSM_LOGI("establish rsp cbk in, addr: %s, tcid: %hhu, src port: %hu, dst port: %hu, mtu: %hu, slqi: %hhu, "
              "status: %hhu, frameType: %hhu", GET_ENC_ADDR(&rsp.addr), rsp.tcid, rsp.srcPort, rsp.dstPort, rsp.mtu,
              rsp.slqi, rsp.status, rsp.frameType);
    if (rsp.status == QOSM_TRANS_CHANNEL_ESTABLISHED) {
        if (QOSM_TransChannelAdd(param->lcid, &rsp) == QOSM_SUCCESS && rsp.frameType != QOSM_SLE_RADIO_FRAME_TYPE_4) {
            QOSM_TransChannelUpdateConnParams(param->lcid);
        }
    }
    if (g_transChannelCbks.statusCbk != NULL) {
        g_transChannelCbks.statusCbk(&rsp);
    }
}

static void QOSM_TransChannelReleaseRspCbk(const CM_DynTransChanReleaseParamRsp_S *param)
{
    QOSM_CHECK_RETURN(param != NULL, "params is NULL.");

    QOSM_TransChannelRspParams_S rsp = {
        .transMode = 0,
        .lcid = param->lcid,
        .tcid = param->srcTcid,
        .srcPort = param->srcPort,
        .dstPort = param->dstPort,
        .mtu = 0,
        .slqi = 0,
        // 当前无论释放成功还是失败，CM都会清理资源，所以直接上报释放成功
        .status = QOSM_TRANS_CHANNEL_RELEASED,
        .frameType = (QOSM_TransConnFrameType_E)param->frameType,
    };
    (void)memcpy_s(&rsp.addr, sizeof(SLE_Addr_S), &param->addr, sizeof(SLE_Addr_S));
    rsp.status = (param->result == CM_DYN_TRANS_CHAN_RELEASE_SUCCESS ||
                param->result == CM_DYN_TRANS_CHAN_RELEASE_TIMEOUT) ?
                QOSM_TRANS_CHANNEL_RELEASED : QOSM_TRANS_CHANNEL_RELEASE_FAIL;
    QOSM_LOGI("release rsp cbk in, addr: %s, lcid: %hu, tcid: %hhu, src port: %hu, dst port: %hu, status: %hhu, "
              "frameType: %hhu", GET_ENC_ADDR(&rsp.addr), param->lcid, rsp.tcid, rsp.srcPort, rsp.dstPort,
              rsp.status, rsp.frameType);
    if (rsp.status == QOSM_TRANS_CHANNEL_RELEASED) {
        if (QOSM_TransChannelDel(param->lcid, &rsp) == QOSM_SUCCESS && rsp.frameType != QOSM_SLE_RADIO_FRAME_TYPE_4) {
            QOSM_TransChannelUpdateConnParams(param->lcid);
        }
    }
    if (g_transChannelCbks.statusCbk != NULL) {
        g_transChannelCbks.statusCbk(&rsp);
    }
}

static void QOSM_TransChannStatusIndicationCbk(const CM_DynTransChanStatusIndicationRsp_S *param)
{
    QOSM_CHECK_RETURN(param != NULL, "params is NULL.");

    QOSM_TransChannelRspParams_S rsp = {
        .transMode = param->transMode,
        .lcid = param->lcid,
        .tcid = param->srcTcid,
        .srcPort = param->srcPort,
        .dstPort = param->dstPort,
        .mtu = param->mtu > CM_CAP_MIN_MTU ? (param->mtu - CM_CAP_MIN_MTU) : 0,
        .slqi = QOSM_TRANS_CHANNEL_SLQI_MAX,
    };
    if (param->slqiList.slqiNum != 0) {
        rsp.slqi = param->slqiList.slqi[0];
    }
    (void)memcpy_s(&rsp.addr, sizeof(SLE_Addr_S), &param->addr, sizeof(SLE_Addr_S));
    if (param->result == CM_DYN_TRANS_CHAN_STATUS_INDICATION_NORMAL) {
        rsp.status = QOSM_TRANS_CHANNEL_ESTABLISHED;
    } else if (param->result == CM_DYN_TRANS_CHAN_STATUS_INDICATION_DICONNECTED) {
        rsp.status = QOSM_TRANS_CHANNEL_RELEASED;
    } else {
        QOSM_LOGI("unknown result: %hhu", param->result);
        return;
    }

    if (rsp.status == QOSM_TRANS_CHANNEL_ESTABLISHED) {
        (void)QOSM_TransChannelAdd(param->lcid, &rsp);
        QOSM_IncreaseChannelSize(param->lcid);
    } else {
        (void)QOSM_TransChannelDel(param->lcid, &rsp);
        QOSM_DecreaseChannelSize(param->lcid);
    }

    QOSM_LOGI("indication cbk in, addr: %s, tcid: %hhu, src port: %hu, dst port: %hu, mtu: %hu, slqi: %hhu, "
              "status: %hhu", GET_ENC_ADDR(&rsp.addr), rsp.tcid, rsp.srcPort, rsp.dstPort, rsp.mtu, rsp.slqi,
              rsp.status);
    if (g_transChannelCbks.statusCbk != NULL) {
        g_transChannelCbks.statusCbk(&rsp);
    }
}

static bool QOSM_TransChannEstablishedCheckCbk(const CM_DynTransChanEstablishedCheckParam_S *param)
{
    if (g_transChannelCbks.establishedCheck == NULL) {
        return true;
    }
    return g_transChannelCbks.establishedCheck(param->srcPort);
}

static bool QOSM_CheckIpSharePeer(uint16_t lcid, uint16_t srcPort, uint16_t dstPort)
{
    const uint16_t ipSharePort = 30200;
    if (srcPort != ipSharePort && dstPort != ipSharePort) {
        return true;
    }
    if (srcPort != ipSharePort || dstPort != ipSharePort || g_ipSharePeerCheck == NULL) {
        return false;
    }
    CM_LogicLink_S link = { 0 };
    return CM_GetLogicLinkByLcid(lcid, &link) == CM_SUCCESS &&
        g_ipSharePeerCheck(&link.addr, srcPort);
}

static uint32_t QOSM_DynTransChanCbksReg(void)
{
    CM_DynTransChannStateMgrRegPeerCheck(QOSM_CheckIpSharePeer);
    // 注册CM传输通道回调
    CM_DynTransChannelCbks_S cbks = {
        .establishRspCbk = QOSM_TransChannelEstablishRspCbk,
        .releaseRspCbk = QOSM_TransChannelReleaseRspCbk,
        .statusIndicationCbk = QOSM_TransChannStatusIndicationCbk,
        .establishedCheckCbk = QOSM_TransChannEstablishedCheckCbk,
    };

    return CM_RegDynTransChannelCbks(&cbks);
}

static void QOSM_DynTransChanCbksUnreg(void)
{
    CM_DynTransChannStateMgrRegPeerCheck(NULL);
    g_ipSharePeerCheck = NULL;
    // 取消注册CM传输通道回调
    CM_UnRegDynTransChannelCbks();
}

static void QOSM_UpdateSleLogicLinkParamsByChipType(void)
{
    bool isSupport = COMMON_IsSupportConnHighSpeed();
    if (isSupport) {
        g_sleLogicLinkParams[QOSM_TRANS_CHANNEL_SLQI_HIGH].connParams.interval = QOSM_SLE_CONN_HIGH_SPEED_INTERAL_EXT;
        QOSM_LOGI("update g_sleLogicLinkParams conn interval to %hu.",
            g_sleLogicLinkParams[QOSM_TRANS_CHANNEL_SLQI_HIGH].connParams.interval);
    }
}

uint32_t QOSM_TransChannelInit(void)
{
    uint32_t ret;

    ret = QOSM_TransChannelMapInit();
    if (ret != QOSM_SUCCESS) {
        return ret;
    }

    ret = QOSM_DynTransChanCbksReg();
    if (ret != QOSM_SUCCESS) {
        QOSM_TransChannelMapDeInit();
        return ret;
    }

    ret = QOSM_LogicLinkCbksReg();
    if (ret != QOSM_SUCCESS) {
        QOSM_TransChannelMapDeInit();
        QOSM_DynTransChanCbksUnreg();
        return ret;
    }

    SDF_DListHeadInit(&g_transChannelNum.channelNumHead);
    g_transChannelNum.channelTotalNum = 0;
    QOSM_UpdateSleLogicLinkParamsByChipType();

    return QOSM_SUCCESS;
}

static void QOSM_FreeTransChannelNode(SDF_DListEntry_S *arg)
{
    QOSM_TransChannelNumNode *node = (QOSM_TransChannelNumNode *)arg;
    if (node == NULL) {
        return;
    }
    SDF_MemFree(node);
}

void QOSM_TransChannelDeInit(void)
{
    QOSM_LogicLinkCbksUnreg();
    QOSM_DynTransChanCbksUnreg();
    QOSM_TransChannelMapDeInit();

    SDF_DListDestroy(&g_transChannelNum.channelNumHead, QOSM_FreeTransChannelNode);
    g_transChannelNum.channelTotalNum = 0;
}
