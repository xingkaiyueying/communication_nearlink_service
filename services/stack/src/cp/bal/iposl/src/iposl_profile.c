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
#include "iposl_internal.h"

#include <string.h>
#include <stdatomic.h>
#include "cp_worker.h"
#include "sdf_mem.h"
#include "sdf_buff.h"

#include "dtap.h"
#include "nlstk_log.h"

static IposlProfileCallbacks g_callbacks;
static bool g_initialized;
static atomic_uint_fast64_t g_sendGeneration;

const IposlProfileCallbacks *IposlGetCallbacks(void)
{
    return g_initialized ? &g_callbacks : NULL;
}

int32_t IposlProfileInit(const IposlProfileCallbacks *callbacks)
{
    if (callbacks == NULL || callbacks->onPeerSupported == NULL || callbacks->onConfigured == NULL ||
        callbacks->prepareMode == NULL || callbacks->isSecure == NULL || callbacks->canSend == NULL) {
        NLSTK_LOG_ERROR("[IpShare][IPoSL] profile init rejected: callback incomplete");
        return IPOSL_ERR_INVALID_PARAM;
    }
    NLSTK_LOG_INFO("[IpShare][IPoSL] profile init started");
    if (!IposlCodecVerifyGoldenVectors()) {
        NLSTK_LOG_ERROR("[IpShare][IPoSL] profile init failed: codec golden vectors");
        return IPOSL_ERR_INVALID_STATE;
    }
    g_callbacks = *callbacks;
    g_initialized = true;
    int32_t ret = IposlServerInitialize();
    if (ret != IPOSL_SUCCESS) {
        (void)memset(&g_callbacks, 0, sizeof(g_callbacks));
        g_initialized = false;
        NLSTK_LOG_ERROR("[IpShare][IPoSL] profile init failed: server init ret=%d", ret);
        return IPOSL_ERR_SSAP;
    }
    NLSTK_LOG_INFO("[IpShare][IPoSL] profile init completed pi=%u", DTAP_PI_IPV4);
    return IPOSL_SUCCESS;
}

void IposlProfileDeinit(void)
{
    NLSTK_LOG_INFO("[IpShare][IPoSL] profile deinit started");
    atomic_store(&g_sendGeneration, 0);
    IposlClientStop();
    IposlServerDeinit();
    (void)memset(&g_callbacks, 0, sizeof(g_callbacks));
    g_initialized = false;
    NLSTK_LOG_INFO("[IpShare][IPoSL] profile deinit completed");
}

int32_t IposlProfileStartServer(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t addressType, uint8_t mode, uint64_t generation)
{
    if (!g_initialized) {
        NLSTK_LOG_ERROR("[IpShare][IPoSL] server start rejected: profile not initialized");
        return IPOSL_ERR_INVALID_STATE;
    }
    atomic_store(&g_sendGeneration, generation);
    int32_t ret = IposlServerStart(peer, addressType, mode, generation);
    if (ret != IPOSL_SUCCESS) {
        NLSTK_LOG_ERROR("[IpShare][IPoSL] server start failed addressType=%u ret=%d", addressType, ret);
    } else {
        NLSTK_LOG_INFO("[IpShare][IPoSL] server start completed addressType=%u", addressType);
    }
    return ret;
}

void IposlProfileStopServer(void)
{
    atomic_store(&g_sendGeneration, 0);
    IposlServerStop();
    NLSTK_LOG_INFO("[IpShare][IPoSL] server stopped");
}

int32_t IposlProfileProbePeer(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t addressType, uint8_t mode, uint64_t generation)
{
    if (!g_initialized) {
        NLSTK_LOG_ERROR("[IpShare][IPoSL] support probe rejected: profile not initialized");
        return IPOSL_ERR_INVALID_STATE;
    }
    int32_t ret = IposlClientStart(peer, addressType, false, NULL, mode, generation);
    if (ret != IPOSL_SUCCESS) {
        NLSTK_LOG_ERROR("[IpShare][IPoSL] support probe client start failed addressType=%u ret=%d", addressType, ret);
    } else {
        NLSTK_LOG_INFO("[IpShare][IPoSL] support probe client start completed addressType=%u", addressType);
    }
    return ret;
}

int32_t IposlProfileStartTerminal(const uint8_t gateway[IPOSL_LAYER2_ID_LEN], uint8_t addressType,
    const uint8_t localLayer2[IPOSL_LAYER2_ID_LEN], uint8_t mode, uint64_t generation)
{
    if (!g_initialized) {
        NLSTK_LOG_ERROR("[IpShare][IPoSL] terminal start rejected: profile not initialized");
        return IPOSL_ERR_INVALID_STATE;
    }
    atomic_store(&g_sendGeneration, generation);
    int32_t ret = IposlClientStart(gateway, addressType, true, localLayer2, mode, generation);
    if (ret != IPOSL_SUCCESS) {
        NLSTK_LOG_ERROR("[IpShare][IPoSL] terminal client start failed addressType=%u ret=%d", addressType, ret);
    } else {
        NLSTK_LOG_INFO("[IpShare][IPoSL] terminal client start completed addressType=%u", addressType);
    }
    return ret;
}

void IposlProfileStopClient(void)
{
    atomic_store(&g_sendGeneration, 0);
    IposlClientStop();
    NLSTK_LOG_INFO("[IpShare][IPoSL] client stopped");
}

uint16_t IposlProfileIdentityServiceMemberCount(void)
{
    return 0;
}

uint8_t IposlProfileDataProtocolIndicator(void)
{
    return DTAP_PI_IPV4;
}

/* The caller and CP task each own one reference, including post failure/timeout. */
typedef struct {
    atomic_int refs;
    int32_t result;
    uint16_t lcid;
    uint16_t length;
    uint8_t tcid;
    uint8_t pi;
    uint64_t generation;
    uint8_t payload[];
} IposlSendTask;

static void IposlSendRelease(void *arg)
{
    IposlSendTask *task = arg;
    if (atomic_fetch_sub(&task->refs, 1) == 1) {
        SDF_MemFree(task);
    }
}

static void IposlSendOnCp(void *arg)
{
    IposlSendTask *task = arg;
    const IposlProfileCallbacks *callbacks = IposlGetCallbacks();
    if (callbacks == NULL || !callbacks->canSend(task->lcid, task->tcid, task->pi, task->generation)) return;
    SDF_Buff_S *buff = SDF_BuffNewWithReserve(task->length);
    if (buff == NULL) {
        return;
    }
    uint8_t *payload = SDF_BuffAppend(buff, task->length);
    if (payload == NULL) {
        SDF_BuffFree(buff);
        return;
    }
    (void)memcpy(payload, task->payload, task->length);
    DTAP_Data_S packet = {.pi = task->pi, .lcid = task->lcid, .tcid = task->tcid, .buff = buff};
    task->result = (int32_t)DTAP_DataSend(&packet);
    if (task->result != 0) {
        SDF_BuffFree(buff);
        NLSTK_LOG_ERROR("[IpShare][TX] CP DTAP send failed ret=%d", task->result);
    }
}

int32_t IposlProfileSendIpv4(uint16_t lcid, uint8_t tcid, const uint8_t *data, uint16_t length)
{
    return IposlProfileSendIp(lcid, tcid, DTAP_PI_IPV4, data, length, atomic_load(&g_sendGeneration));
}

int32_t IposlProfileSendIp(uint16_t lcid, uint8_t tcid, uint8_t pi, const uint8_t *data,
    uint16_t length, uint64_t generation)
{
    if (!IposlCodecValidatePacket(pi, data, length)) {
        return IPOSL_ERR_INVALID_PARAM;
    }
    IposlSendTask *task = SDF_MemAlloc(sizeof(*task) + length);
    if (task == NULL) {
        return IPOSL_ERR_INVALID_STATE;
    }
    atomic_init(&task->refs, 2);
    task->result = IPOSL_ERR_INVALID_STATE;
    task->lcid = lcid;
    task->tcid = tcid;
    task->pi = pi;
    task->generation = generation;
    task->length = length;
    (void)memcpy(task->payload, data, length);
    /* Like Transport, wait at most 500 ms. The task owns its copy after timeout. */
    uint32_t posted = CP_PostTaskBlocked(IposlSendOnCp, task, IposlSendRelease, 500);
    int32_t result = posted == 0 ? task->result : IPOSL_ERR_INVALID_STATE;
    IposlSendRelease(task);
    return result;
}
