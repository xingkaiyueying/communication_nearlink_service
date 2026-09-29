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

#include "cm_dyn_trans_chan_state_mgr.h"
#include "securec.h"
#include "cm_log.h"
#include "cm_errno.h"
#include "cm_def.h"
#include "cm_dyn_tcid.h"
#include "cm_signaling_trans_channel.h"
#include "cm_dyn_trans_channel_api.h"

static CM_DynTransChannelCbks_S g_channelCbks = { 0 };

static uint32_t CM_DynTransChanActiveEstablishingReqHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState);
static uint32_t CM_DynTransChanActiveEstablishedRspHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState);
static uint32_t CM_DynTransChanActiveEstablishingFailRspHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState);
static uint32_t CM_DynTransChanActiveReleasingHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState);
static uint32_t CM_DynTransChanActiveReleasedRspHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState);
static uint32_t CM_DynTransChanActiveReleasingFailRspHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState);
static uint32_t CM_DynTransChanPassiveReleasedReqHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState);
static uint32_t CM_DynTransChanPassiveEstablishedReqHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState);

static CM_DynTransChanHandlerEvent g_dynTransChanEvents[] = {
    {
        CM_TRANS_CHANNEL_STATE_INIT,
        CM_DYN_TRANS_CHAN_EVENT_ACTIVE_ESTABLISHING_REQ,
        CM_TRANS_CHANNEL_STATE_ACTIVATING,
        CM_DynTransChanActiveEstablishingReqHandler
    },
    {
        CM_TRANS_CHANNEL_STATE_ACTIVATING,
        CM_DYN_TRANS_CHAN_EVENT_ACTIVE_ESTABLISHED_RSP,
        CM_TRANS_CHANNEL_STATE_ACTIVATED,
        CM_DynTransChanActiveEstablishedRspHandler,
    },
    {
        CM_TRANS_CHANNEL_STATE_ACTIVATING,
        CM_DYN_TRANS_CHAN_EVENT_ACTIVE_ESTABLISHING_FAIL_RSP,
        CM_TRANS_CHANNEL_STATE_RELEASED,
        CM_DynTransChanActiveEstablishingFailRspHandler,
    },
    {
        CM_TRANS_CHANNEL_STATE_ACTIVATED,
        CM_DYN_TRANS_CHAN_EVENT_ACTIVE_RELEASING_REQ,
        CM_TRANS_CHANNEL_STATE_RELEASING,
        CM_DynTransChanActiveReleasingHandler,
    },
    {
        CM_TRANS_CHANNEL_STATE_RELEASING,
        CM_DYN_TRANS_CHAN_EVENT_ACTIVE_RELEASED_RSP,
        CM_TRANS_CHANNEL_STATE_RELEASED,
        CM_DynTransChanActiveReleasedRspHandler,
    },
    {
        CM_TRANS_CHANNEL_STATE_RELEASING,
        CM_DYN_TRANS_CHAN_EVENT_ACTIVE_RELEASING_FAIL_RSP,
        CM_TRANS_CHANNEL_STATE_RELEASED,
        CM_DynTransChanActiveReleasingFailRspHandler,
    },
    {
        CM_TRANS_CHANNEL_STATE_ACTIVATED,
        CM_DYN_TRANS_CHAN_EVENT_PASSIVE_RELEASED_REQ,
        CM_TRANS_CHANNEL_STATE_RELEASED,
        CM_DynTransChanPassiveReleasedReqHandler,
    },
    {
        CM_TRANS_CHANNEL_STATE_INIT,
        CM_DYN_TRANS_CHAN_EVENT_PASSIVE_ESTABLISHED_REQ,
        CM_TRANS_CHANNEL_STATE_ACTIVATED,
        CM_DynTransChanPassiveEstablishedReqHandler,
    },
};

void CM_DynTransChannStateMgrRegCbks(const CM_DynTransChannelCbks_S *cbks)
{
    CM_DynTcidInit();
    g_channelCbks = *cbks;
}

void CM_DynTransChannStateMgrUnRegCbks(void)
{
    (void)memset_s(&g_channelCbks, sizeof(g_channelCbks), 0x00, sizeof(g_channelCbks));
    CM_DynTcidDeInit();
}

/*
 * @brief 传输通道主动建立请求事件
 */
static uint32_t CM_DynTransChanActiveEstablishingReqHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState)
{
    CM_LOGD("dyn chan active establishing lcid:0x%04x, currentState:%hhu, expectedTransportMode:%hhu, result:%hhu, "
        "nextState:%hhu", param->lcid, param->state, param->expectedTransportMode, param->result, nextState);
    // 1. allocate tcid
    uint8_t localTcid = CM_DynTcidAllocate(param->lcid, param->expectedTransportMode);
    CM_CHECK_RETURN_RET((localTcid != CM_TRANS_INVALID_TCID), CM_FAIL, "get avaliable localTcid failed");

    // 2. active dyn trans channel node
    CM_DynTransChanParam_S activeParam = { 0 };
    (void)memcpy_s(&activeParam, sizeof(CM_DynTransChanParam_S), param, sizeof(CM_DynTransChanParam_S));
    activeParam.localTcid = localTcid;
    activeParam.remoteTcid = CM_TRANS_INVALID_TCID;
    activeParam.state = nextState;
    SleTransLcid_S *node = CM_ActivateDynamicTransChannel(param->lcid, &activeParam);
    if (node == NULL) {
        CM_LOGE("activate dyn trans channel failed, lcid:0x%04x", param->lcid);
        (void)CM_DynTcidRelease(param->lcid, localTcid, param->expectedTransportMode);
        return CM_FAIL;
    }

    // 3. send establish req signaling
    CM_SignalingTransChanEstablishReq_S req = { 0 };
    req.srcTcid = localTcid;
    req.exclusive = param->exclusive;
    req.measure = param->measure;
    if (memcpy_s(&req.transModeConfig, sizeof(CM_SignalingTransModeConfig_S),
        &param->transModeConfig, sizeof(CM_TransModeConfig_S)) != EOK) {
        goto CM_DYN_TRANS_CHAN_ACTIVE_ESATBLISHING_FAIL;
    }
    CM_SignalingPortConfig_S portConfig = { 0 };
    portConfig.srcPort = param->srcPort;
    portConfig.dstPort = param->dstPort;
    portConfig.aid = param->aid;
    req.extension.portConfig = &portConfig;
    req.slqiList.slqiNum = CM_TRANS_CHAN_SLQI_SURPPORT_NUM;
    req.slqiList.slqi[0] = param->slqi;

    uint32_t ret = CM_SignalingTransChanEstablishReqSend(param->lcid, &req);
    if (ret != CM_SUCCESS) {
        CM_LOGE("CM_SignalingTransChanEstablishReqSend failed, lcid:0x%04x", param->lcid);
        goto CM_DYN_TRANS_CHAN_ACTIVE_ESATBLISHING_FAIL;
    }
    CM_LOGD("dyn chann active establishing req lcid:0x%04x, localTcid:0x%02x, state:0x%02x",
        param->lcid, localTcid, activeParam.state);
    return CM_SUCCESS;
CM_DYN_TRANS_CHAN_ACTIVE_ESATBLISHING_FAIL:
    CM_ReleaseDynamicTransChannelByLocalTcid(node);
    (void)CM_DynTcidRelease(param->lcid, localTcid, param->expectedTransportMode);
    return CM_FAIL;
}

/*
 * @brief 通知Qosm模块
 */
static void CM_DynTransChanEstablishRspCbk(const SleTransLcid_S *node, uint8_t result)
{
    if (g_channelCbks.establishRspCbk == NULL) {
        return;
    }
    const SleTransLcidParam_S *param = node->params;
    CM_CHECK_RETURN(param != NULL, "param is null");
    CM_DynTransChanEstablishParamRsp_S rsp = { 0 };
    rsp.version = param->version;
    rsp.localIndex = param->localIndex;
    rsp.lcid = node->lcid;
    rsp.transMode = param->transModeConfig.transMode;
    rsp.srcTcid = node->lastTransid[CM_TRANS_LOCAL];
    rsp.dstTcid = node->lastTransid[CM_TRANS_REMOTE];
    rsp.srcPort = param->srcPort;
    rsp.dstPort = param->dstPort;
    rsp.aidList.aidNum = CM_TRANS_CHAN_AID_SURPPORT_NUM;
    rsp.aidList.aid[0] = param->aid;
    rsp.slqiList.slqiNum = CM_TRANS_CHAN_SLQI_SURPPORT_NUM;
    rsp.slqiList.slqi[0] = param->slqi;
    rsp.result = result;
    rsp.mtu = param->transModeConfig.mtu;
    rsp.frameType = param->frameType;
    (void)memcpy_s(&rsp.addr, sizeof(rsp.addr), &node->addr, sizeof(node->addr));
    CM_LOGD("establishRspCbk, lcid:0x%04x, srcTcid:0x%02x, dstTcid:0x%02x, "
        "srcPort: 0x%04x, dstPort: 0x%04x, addr:%s, result:0x%02x, mtu: 0x%04x", rsp.lcid, rsp.srcTcid, rsp.dstTcid,
        param->srcPort, param->dstPort, GetEncryptAddr(&rsp.addr).buf, rsp.result, rsp.mtu);
    g_channelCbks.establishRspCbk(&rsp);
}

static void CM_DynTransChanReleaseRspCbk(const SleTransLcid_S *node, uint8_t result)
{
    if (g_channelCbks.releaseRspCbk == NULL) {
        return;
    }
    const SleTransLcidParam_S *param = node->params;
    CM_CHECK_RETURN(param != NULL, "param is null");
    CM_DynTransChanReleaseParamRsp_S rsp = { 0 };
    rsp.version = param->version;
    rsp.localIndex = param->localIndex;
    rsp.lcid = node->lcid;
    rsp.srcTcid = node->lastTransid[CM_TRANS_LOCAL];
    rsp.dstTcid = node->lastTransid[CM_TRANS_REMOTE];
    rsp.srcPort = param->srcPort;
    rsp.dstPort = param->dstPort;
    rsp.result = result;
    rsp.slqiList.slqiNum = CM_TRANS_CHAN_SLQI_SURPPORT_NUM;
    rsp.slqiList.slqi[0] = param->slqi;
    rsp.frameType = param->frameType;
    (void)memcpy_s(&rsp.addr, sizeof(rsp.addr), &node->addr, sizeof(node->addr));
    CM_LOGD("releaseRspCbk, lcid:0x%04x, srcTcid:0x%02x, dstTcid:0x%02x, srcPort: 0x%04x, dstPort: 0x%04x, "
        "result:0x%02x, rsp.frameType:%hhu", rsp.lcid, rsp.srcTcid, rsp.dstTcid, param->srcPort, param->dstPort,
        rsp.result, rsp.frameType);
    g_channelCbks.releaseRspCbk(&rsp);
}

static void CM_DynTransChanStatusIndicationCbk(const SleTransLcid_S *node, bool added, uint8_t result)
{
    if (g_channelCbks.statusIndicationCbk == NULL) {
        return;
    }
    const SleTransLcidParam_S *param = node->params;
    CM_CHECK_RETURN(param != NULL, "param is null");
    CM_DynTransChanStatusIndicationRsp_S rsp = { 0 };
    rsp.version = param->version;
    rsp.localIndex = param->localIndex;
    rsp.lcid = node->lcid;
    rsp.transMode = param->transModeConfig.transMode;
    rsp.srcTcid = node->lastTransid[CM_TRANS_LOCAL];
    rsp.dstTcid = node->lastTransid[CM_TRANS_REMOTE];
    rsp.added = added;
    rsp.srcPort = param->srcPort;
    rsp.dstPort = param->dstPort;
    rsp.result = result;
    rsp.slqiList.slqiNum = CM_TRANS_CHAN_SLQI_SURPPORT_NUM;
    rsp.slqiList.slqi[0] = param->slqi;
    rsp.mtu = param->transModeConfig.mtu;
    (void)memcpy_s(&rsp.addr, sizeof(rsp.addr), &node->addr, sizeof(node->addr));
    CM_LOGD("statusIndicationCbk lcid:0x%04x, srcTcid:0x%02x, dstTcid:0x%02x, srcPort: 0x%04x, dstPort: 0x%04x, "
        "added:%hhu, result:0x%02x, mtu: 0x%04x", rsp.lcid, rsp.srcTcid, rsp.dstTcid, param->srcPort, param->dstPort,
        rsp.added, rsp.result, rsp.mtu);
    g_channelCbks.statusIndicationCbk(&rsp);
}

/*
 * @brief 收到传输通道主动建立成功响应事件
 */
static uint32_t CM_DynTransChanActiveEstablishedRspHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState)
{
    (void)curState;
    CM_LOGD("active established rsp handler lcid:0x%04x, currentState:%hhu, expectedTransportMode:%hhu, result:%hhu, "
        "nextState:%hhu, localTcid:0x%02x, remoteTcid:0x%02x", param->lcid, param->state, param->expectedTransportMode,
        param->result, nextState, param->localTcid, param->remoteTcid);
    if (param->result != CM_RESULT_ESTABLISH_SUCCESS) {
        return CM_FAIL;
    }
    SleTransLcid_S *node = CM_FindTransChannelByLocalTcid(param->lcid, param->localTcid);
    CM_CHECK_RETURN_RET(node != NULL, CM_FAIL, "find trans chann by lcid 0x%04x, localTcid:0x%02x failed.",
        param->lcid, param->localTcid);
    // 1. established set dyn channel remote tcid
    CM_DynamicTransChannelSetTcidAndChangeCbk(node, param->remoteTcid, nextState);
    // 2. established rsp cbk
    CM_DynTransChanEstablishRspCbk(node, CM_DYN_TRANS_CHAN_ESTABLISH_SUCCESS);
    return CM_SUCCESS;
}

/*
 * @brief 收到传输通道主动建立失败响应事件，比如超时
 */
static uint32_t CM_DynTransChanActiveEstablishingFailRspHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState)
{
    (void)curState;
    CM_LOGD("active establishing fail rsp handler expectedTransportMode:%hhu, result:%hhu, "
        "nextState:%hhu, localTcid:0x%02x, remoteTcid:0x%02x", param->expectedTransportMode, param->result,
        nextState, param->localTcid, param->remoteTcid);
    if (param->result == CM_RESULT_ESTABLISH_SUCCESS) {
        return CM_FAIL;
    }
    SleTransLcid_S *node = CM_FindTransChannelByLocalTcid(param->lcid, param->localTcid);
    CM_CHECK_RETURN_RET(node != NULL, CM_FAIL, "find trans chann by lcid 0x%04x, tcid:0x%02x failed.",
        param->lcid, param->localTcid);
    uint8_t result = CM_DYN_TRANS_CHAN_ESTABLISH_FAIL;
    if (param->result == CM_RESULT_ESTABLISH_TIMEOUT) {
        result = CM_DYN_TRANS_CHAN_ESTABLISH_TIMEOUT;
    }
    // 1. establish fail rsp cbk
    CM_DynTransChanEstablishRspCbk(node, result);
    // 2. release dyn channel node
    CM_ReleaseDynamicTransChannelByLocalTcid(node);
    // 3. release dyn tcid
    uint32_t ret = CM_DynTcidRelease(param->lcid, param->localTcid, param->expectedTransportMode);
    CM_CHECK_RETURN_RET(ret == CM_SUCCESS, CM_FAIL,
        "release tcid failed, lcid:0x%04x, localTcid:0x%02x", param->lcid, param->localTcid);
    return CM_SUCCESS;
}

/*
 * @brief 传输通道主动释放请求事件
 */
static uint32_t CM_DynTransChanActiveReleasingHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState)
{
    CM_LOGD("active releasing handler currentState:%hhu, expectedTransportMode:%hhu, result:%hhu, nextState:%hhu, "
        "localTcid:0x%02x, remoteTcid:0x%02x", param->state, param->expectedTransportMode, param->result, nextState,
        param->localTcid, param->remoteTcid);
    CM_DynTransChanParam_S setParam = { 0 };
    setParam.lcid = param->lcid;
    setParam.localTcid = param->localTcid;
    // 1. set dyn channel state
    SleTransLcid_S *node = CM_FindTransChannelByLocalTcid(setParam.lcid, setParam.localTcid);
    CM_CHECK_RETURN_RET((node != NULL), CM_FAIL, "find trans channel by lcid:0x%04x, localTcid:0x%02x failed",
        setParam.lcid, setParam.localTcid);
    node->state = nextState;
    // 2. send release req signaling
    CM_SignalingTransChanReleaseReq_S releaseReq = { 0 };
    releaseReq.srcTcid = param->localTcid;
    releaseReq.dstTcid = node->lastTransid[CM_TRANS_REMOTE]; // param->remoteTcid 入参为0
    CM_LOGI("lcid:0x%04x, localTcid:0x%02x, curState:0x%04x, nextState:0x%04x, remoteTcid:0x%02x",
        setParam.lcid, setParam.localTcid, node->state, nextState, releaseReq.dstTcid);
    uint32_t ret = CM_SignalingTransChanReleaseReqSend(param->lcid, &releaseReq);
    if (ret != CM_SUCCESS) {
        CM_LOGE("send signaling release trans chan req failed, lcid:0x%04x, localTcid:0x%02x, remoteTcid:0x%02x, "
            "curState:0x%04x, nextState:0x%04x", param->lcid, releaseReq.srcTcid, releaseReq.dstTcid, curState,
            node->state);
        node->state = curState;
        return CM_FAIL;
    }
    return CM_SUCCESS;
}

/**
 * 收到传输通道主动释放成功响应事件
 */
static uint32_t CM_DynTransChanActiveReleasedRspHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState)
{
    (void)curState;
    CM_LOGD("active released rsp handler currentState:%hhu, expectedTransportMode:%hhu, result:%hhu, "
        "nextState:%hhu, localTcid:0x%02x, remoteTcid:0x%02x",
        param->state, param->expectedTransportMode, param->result, nextState, param->localTcid, param->remoteTcid);
    SleTransLcid_S *node = CM_FindTransChannelByLocalTcid(param->lcid, param->localTcid);
    CM_CHECK_RETURN_RET(node != NULL, CM_FAIL, "find trans chann by lcid 0x%04x, tcid:0x%02x failed.",
        param->lcid, param->localTcid);
    // 1. release rsp cbk
    CM_DynTransChanReleaseRspCbk(node, CM_DYN_TRANS_CHAN_RELEASE_SUCCESS);
    // 2. release dyn channel node
    CM_ReleaseDynamicTransChannelByLocalTcid(node);
    // 3. release dyn tcid
    uint32_t ret = CM_DynTcidRelease(param->lcid, param->localTcid, param->expectedTransportMode);
    CM_CHECK_RETURN_RET(ret == CM_SUCCESS, CM_FAIL,
        "release tcid failed, lcid:0x%04x, srcTcid:0x%02x", param->lcid, param->localTcid);
    return CM_SUCCESS;
}

/**
 * @brief 传输通道主动释放信令失败事件，可能为超时
 */
static uint32_t CM_DynTransChanActiveReleasingFailRspHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState)
{
    (void)curState;
    CM_LOGD("active releasing fail rsp handler currentState:%hhu, expectedTransportMode:%hhu, result:%hhu, "
        "nextState:%hhu, lcid:0x%04x, localTcid:0x%02x, remoteTcid:0x%02x", param->state, param->expectedTransportMode,
        param->result, nextState, param->lcid, param->localTcid, param->remoteTcid);
    SleTransLcid_S *node = CM_FindTransChannelByLocalTcid(param->lcid, param->localTcid);
    CM_CHECK_RETURN_RET(node != NULL, CM_FAIL, "find trans chann by lcid 0x%04x, tcid:0x%02x failed.",
        param->lcid, param->localTcid);
    uint8_t result = CM_DYN_TRANS_CHAN_RELEASE_FAIL;
    if (param->result == CM_RESULT_RELEASE_TIMEOUT) {
        result = CM_DYN_TRANS_CHAN_RELEASE_TIMEOUT;
    }
    // 1. release rsp cbk
    CM_DynTransChanReleaseRspCbk(node, result);
    // 2. release dyn channel node
    CM_ReleaseDynamicTransChannelByLocalTcid(node);
    // 3. release dyn tcid
    uint32_t ret = CM_DynTcidRelease(param->lcid, param->localTcid, param->expectedTransportMode);
    CM_CHECK_RETURN_RET(ret == CM_SUCCESS, CM_FAIL,
        "release tcid failed, lcid:0x%04x, srcTcid:0x%02x", param->lcid, param->localTcid);
    return CM_SUCCESS;
}

/**
 * @brief 异常流程，发送断开请求给对端
 */
static void CM_DynTransChanExpectionReleaseHandler(uint16_t lcid, uint8_t localTcid, uint8_t remoteTcid, uint8_t reqId)
{
    CM_SignalingTransChanReleaseReq_S req = { 0 };
    req.srcTcid = localTcid;
    req.dstTcid = remoteTcid;
    uint32_t ret = CM_SignalingTransChanReleaseReqSend(lcid, &req);
    if (ret != CM_SUCCESS) {
        CM_LOGE("CM_SignalingTransChanReleaseReqSend failed, lcid:0x%04x, reqId:0x%02x", lcid, reqId);
        // 继续返回失败，那还是失败了
    }
}

/**
 * @brief 收到传输通道被动释放请求事件
 */
static uint32_t CM_DynTransChanPassiveReleasedReqHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState)
{
    (void)curState;
    CM_LOGD("passive released rsp handler currentState:%hhu, expectedTransportMode:%hhu, result:%hhu, "
        "nextState:%hhu, localTcid:0x%02x, remoteTcid:0x%02x", param->state, param->expectedTransportMode,
        param->result, nextState, param->localTcid, param->remoteTcid);
    SleTransLcid_S *node = CM_FindTransChannelByRemoteTcid(param->lcid, param->remoteTcid);
    CM_CHECK_RETURN_RET((node != NULL), CM_FAIL, "find lcid 0x%04x failed, release dyn remoteTcid:0x%02x failed.",
        param->lcid, param->remoteTcid);
    uint8_t localTcid = node->lastTransid[CM_TRANS_LOCAL];
    uint8_t remoteTcid = node->lastTransid[CM_TRANS_REMOTE];
    uint16_t lcid = node->lcid;
    // 1. status indication cbk
    CM_DynTransChanStatusIndicationCbk(node, false, CM_DYN_TRANS_CHAN_STATUS_INDICATION_DICONNECTED);
    CM_LOGD("passive released rsp handler currentState:%hhu, expectedTransportMode:%hhu, result:%hhu, "
        "nextState:%hhu, localTcid:0x%02x, remoteTcid:0x%02x", param->state, param->expectedTransportMode,
        param->result, nextState, param->localTcid, param->remoteTcid);
    // 2. release tcid, Note: param->localTcid is invalid
    uint32_t ret = CM_DynTcidRelease(lcid, localTcid, param->expectedTransportMode);
    if (ret != CM_SUCCESS) {
        CM_LOGE("release tcid failed, lcid:0x%04x, localTcid:0x%02x", lcid, localTcid);
        // continue to do other
    }
    // 3. release dyn channel node
    CM_ReleaseDynamicTransChannelByRemoteTcid(node);
    // 4. send release rsp signaling
    CM_SignalingTransChanReleaseRsp_S rsp = { 0 };
    rsp.dstTcid = localTcid;  // 被动请求回复响应，dstTcid 为本端的 tcid
    rsp.srcTcid = remoteTcid; // 被动请求回复响应，srcTcid 为对端的 tcid
    rsp.result = CM_RESULT_RELEASE_SUCCESS;
    ret = CM_SignalingTransChanReleaseRspSend(lcid, param->reqId, &rsp);
    if (ret != CM_SUCCESS) {
        CM_LOGE("CM_SignalingTransChanReleaseRspSend failed, lcid:0x%04x, reqId:0x%02x", lcid, param->reqId);
        // 发送响应失败，尝试发送一次主动发送释放请求操作
        CM_DynTransChanExpectionReleaseHandler(lcid, localTcid, remoteTcid, param->reqId);
        return CM_FAIL;
    }
    return CM_SUCCESS;
}

static bool CM_DynTransChannelEstablishedCheck(uint16_t lcid, uint16_t srcPort, uint16_t dstPort)
{
    if (g_channelCbks.establishedCheckCbk == NULL) {
        CM_LOGW("establishedCheckCbk is null");
        return true;
    }

    CM_DynTransChanEstablishedCheckParam_S param = { .lcid = lcid, .srcPort = srcPort, .dstPort = dstPort };
    return g_channelCbks.establishedCheckCbk(&param);
}

static bool CM_DynTransChannelMtuCheck(uint16_t mtu)
{
    return mtu >= CM_CAP_MIN_MTU;
}

static void CM_DynTransChanSendEstablishedErrResp(const CM_DynTransChanParam_S *param, uint8_t result)
{
    CM_SignalingTransChanEstablishRsp_S establishRsp = { 0 };
    establishRsp.srcTcid = param->remoteTcid;  // 被动请求回复响应，srcTcid 为对端的 tcid
    establishRsp.dstTcid = CM_TRANS_INVALID_TCID;
    establishRsp.result = result;
    if (CM_SignalingTransChanEstablishRspSend(param->lcid, param->reqId, &establishRsp) != CM_SUCCESS) {
        CM_LOGE("signaling establish rsp sent failed, lcid:0x%04x, reqId:0x%02x", param->lcid, param->reqId);
    }
}

/**
 * @brief 收到传输通道建立请求事件
 */
static uint32_t CM_DynTransChanPassiveEstablishedReqHandler(uint8_t curState, const CM_DynTransChanParam_S *param,
    uint8_t nextState)
{
    (void)curState;
    CM_LOGD("passive established req handler currentState:%hhu, expectedTransportMode:%hhu, result:%hhu, "
        "nextState:%hhu, localTcid:0x%02x, remoteTcid:0x%02x, srcPort: 0x%04x, dstPort: 0x%04x",
        param->state, param->expectedTransportMode, param->result, nextState, param->localTcid, param->remoteTcid,
        param->srcPort, param->dstPort);
    if (!CM_DynTransChannelMtuCheck(param->transModeConfig.mtu)) {
        CM_LOGW("established req check failed, invalid mtu: %hu", param->transModeConfig.mtu);
        CM_DynTransChanSendEstablishedErrResp(param, CM_RESULT_UNSUPPORTED_MTU_SIZE);
        return CM_FAIL;
    }
    if (!CM_DynTransChannelEstablishedCheck(param->lcid, param->srcPort, param->dstPort)) {
        CM_LOGW("established req check failed, srcPort: 0x%04x, dstPort: 0x%04x", param->srcPort, param->dstPort);
        CM_DynTransChanSendEstablishedErrResp(param, CM_RESULT_INSUFFICIENT_RESOURCE);
        return CM_FAIL;
    }

    // 1. allocate local tcid
    uint8_t localTcid = CM_DynTcidAllocate(param->lcid, param->expectedTransportMode);
    CM_CHECK_RETURN_RET((localTcid != CM_TRANS_INVALID_TCID), CM_FAIL, "get avaliable localTcid failed");
    // 2. active dyn trans channel node
    CM_DynTransChanParam_S activeParam = { 0 };
    (void)memcpy_s(&activeParam, sizeof(CM_DynTransChanParam_S), param, sizeof(CM_DynTransChanParam_S));
    activeParam.localTcid = localTcid;
    activeParam.state = nextState;
    SleTransLcid_S *node = CM_ActivateDynamicTransChannel(param->lcid, &activeParam);
    if (node == NULL) {
        CM_LOGE("activate dyn trans channel failed, lcid:0x%04x", param->lcid);
        (void)CM_DynTcidRelease(param->lcid, localTcid, param->expectedTransportMode);
        return CM_FAIL;
    }
    // 3. send establish rsp signaling
    CM_SignalingTransChanEstablishRsp_S establishRsp = { 0 };
    establishRsp.srcTcid = param->remoteTcid;  // 被动请求回复响应，srcTcid 为对端的 tcid
    establishRsp.dstTcid = localTcid;          // 被动请求回复响应，dstTcid 为本端的 tcid
    establishRsp.result = CM_RESULT_ESTABLISH_SUCCESS;
    uint32_t ret = CM_SignalingTransChanEstablishRspSend(param->lcid, param->reqId, &establishRsp);
    if (ret != CM_SUCCESS) {
        CM_LOGE("signaling establish rsp sent failed failed, lcid:0x%04x, reqId:0x%02x", param->lcid, param->reqId);
        CM_ReleaseDynamicTransChannelByLocalTcid(node);
        (void)CM_DynTcidRelease(param->lcid, localTcid, param->expectedTransportMode);
        return CM_FAIL;
    }
    // 4. status indication cbk
    CM_DynTransChanStatusIndicationCbk(node, true, CM_DYN_TRANS_CHAN_STATUS_INDICATION_NORMAL);
    return CM_SUCCESS;
}

uint32_t CM_DynTransChanPostEventAndHandler(uint8_t event, const CM_DynTransChanParam_S *param)
{
    CM_CHECK_RETURN_RET(param != NULL, CM_NULL_POINTER, "param is null");
    CM_LOGI("cm dyn trans handle event=%hhu, currentState=%hhu", event, param->state);

    size_t eventSize = sizeof(g_dynTransChanEvents) / sizeof(g_dynTransChanEvents[0]);
    CM_CHECK_RETURN_RET(event < eventSize, CM_FAIL, "event:%hhu is invalid", event);
    if (g_dynTransChanEvents[event].func == NULL) {
        CM_LOGE("func is null, cm dyn trans ignore handle message, event=%hhu, currentState=%hhu", event, param->state);
        return CM_FAIL;
    }
    return g_dynTransChanEvents[event].func(g_dynTransChanEvents[event].currentState, param,
        g_dynTransChanEvents[event].nextState);
}

static void CM_DynTransChannelNotifyAllDisconnected(SDF_Vector_S *transChannelVector)
{
    for (uint32_t i = 0; i < transChannelVector->size; i++) {
        SleTransLcid_S *node = SDF_VectorElementAt(transChannelVector, i);
        CM_CHECK_RETURN(node != NULL, "node is null");
        uint8_t state = node->state;
        if (state == CM_TRANS_CHANNEL_STATE_RELEASED) {
            /* No reached here */
            continue;
        }
        // 1. status indication cbk
        if (state == CM_TRANS_CHANNEL_STATE_ACTIVATING || state == CM_TRANS_CHANNEL_STATE_INIT) {
            CM_DynTransChanEstablishRspCbk(node, CM_DYN_TRANS_CHAN_ESTABLISH_FAIL);
        } else if (state == CM_TRANS_CHANNEL_STATE_ACTIVATED || state == CM_TRANS_CHANNEL_STATE_RELEASING) {
            CM_DynTransChanStatusIndicationCbk(node, false, CM_DYN_TRANS_CHAN_STATUS_INDICATION_DICONNECTED);
        } else {
            CM_LOGE("dyn trans channel state:%hhu not support", state);
            continue;
        }
        // 2. release dyn channel node
        CM_ReleaseDynamicTransChannelByLocalTcid(node);
    }
}

void CM_DynTransChannReleaseAllNode(uint16_t lcid)
{
    SDF_Traits transChannelTraits = { .dtor = NULL };
    SDF_Vector_S *transChannelVector = SDF_CreateVector(transChannelTraits);
    CM_CHECK_RETURN((transChannelVector != NULL), "SDF_CreateVector transChannel failed");
    uint32_t ret = CM_FindDynTransChannelByLcid(lcid, transChannelVector);
    if (ret != CM_SUCCESS) {
        if (ret == CM_NOT_FOUND) {
            CM_LOGD("CM_DynTransChannReleaseAllNode no trans channel need release, lcid: 0x%04x", lcid);
            SDF_DestroyVector(transChannelVector);
            return;
        }
        SDF_DestroyVector(transChannelVector);
        CM_LOGE("find lcid 0x%04x failed, release dyn tcid failed.", lcid);
        return;
    }
    CM_DynTransChannelNotifyAllDisconnected(transChannelVector);
    SDF_DestroyVector(transChannelVector);
}