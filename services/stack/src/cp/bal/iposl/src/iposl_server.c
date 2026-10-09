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
#include <stdlib.h>
#include <stdatomic.h>

#include "cp_worker.h"
#include "sdf_mem.h"

#include "iposl_codec.h"
#include "nlstk_log.h"
#include "nlstk_ssap_app_server.h"

static const uint8_t g_identifierUuid[16] = {
    0x8F, 0x6F, 0x1D, 0x00, 0x7B, 0x0C, 0x4A, 0x73, 0x9D, 0x4E, 0x6E, 0x65, 0x61, 0x72, 0x6C, 0x01
};
static const uint8_t g_configUuid[16] = {
    0x5B, 0x28, 0x50, 0x05, 0x45, 0x55, 0x4F, 0xA5, 0xB9, 0x57, 0xC2, 0x9C, 0x0D, 0x8A, 0x60, 0xC3
};
static const uint8_t g_terminalCapabilityUuid[16] = {
    0xFA, 0x36, 0x1B, 0x52, 0xCB, 0x0A, 0x49, 0xED, 0x84, 0x27, 0x1A, 0x67, 0x5A, 0xCC, 0xD9, 0xDB
};
static const uint8_t g_terminalStateUuid[16] = {
    0xE4, 0x72, 0x0A, 0xB0, 0xE6, 0x2A, 0x42, 0xD2, 0xAD, 0x96, 0x98, 0x62, 0x7C, 0xEE, 0xAE, 0xDB
};
static const uint8_t g_gatewayCapabilityUuid[16] = {
    0x2E, 0x68, 0xC5, 0x66, 0xE3, 0x14, 0x46, 0x6D, 0x96, 0x29, 0x40, 0x2F, 0x16, 0xCA, 0xB2, 0x19
};
static const uint8_t g_gatewayStateUuid[16] = {
    0x43, 0x11, 0x7F, 0xB2, 0x90, 0x9E, 0x4B, 0x70, 0xB6, 0xF9, 0x10, 0x52, 0xD7, 0x29, 0xC8, 0x9B
};
static const uint8_t g_methodUuid[16] = {
    0x7A, 0xA3, 0x12, 0x0E, 0xF0, 0xD2, 0x45, 0x60, 0xB7, 0x11, 0xA5, 0xB6, 0x18, 0xB7, 0xA3, 0x2B
};

static atomic_int g_serverAppId = SSAP_APP_INVALID_ID;
static uint8_t g_expectedPeer[IPOSL_LAYER2_ID_LEN];
static uint8_t g_expectedAddressType;
static uint8_t g_configuredLayer2[IPOSL_LAYER2_ID_LEN];
static bool g_configured;
static bool g_serverActive;
static uint8_t g_allowedMode;
static uint8_t g_selectedMode;
static bool g_enabled;
static uint64_t g_generation;

/* Allocated to the product's configured capacity, never to a fixed peer count. */
typedef struct {
    bool used;
    bool enabled;
    uint8_t peer[IPOSL_LAYER2_ID_LEN];
    uint8_t addressType;
    uint8_t layer2[IPOSL_LAYER2_ID_LEN];
    uint8_t mode;
} IposlServerPeer;
static IposlServerPeer *g_peers;
static uint32_t g_peerCapacity;
static bool g_anyPeer;

static IposlServerPeer *FindPeer(const SLE_Addr_S *addr)
{
    if (addr == NULL || !g_anyPeer) return NULL;
    for (uint32_t i = 0; i < g_peerCapacity; ++i) {
        if (g_peers[i].used && g_peers[i].addressType == addr->type &&
            memcmp(g_peers[i].peer, addr->addr, IPOSL_LAYER2_ID_LEN) == 0) return &g_peers[i];
    }
    return NULL;
}

static IposlServerPeer *FreePeer(void)
{
    for (uint32_t i = 0; i < g_peerCapacity; ++i) if (!g_peers[i].used) return &g_peers[i];
    return NULL;
}

static void SetUuid(NLSTK_SsapUuid_S *uuid, const uint8_t value[16])
{
    (void)memcpy(uuid->uuid, value, sizeof(uuid->uuid));
}

static bool IsExpectedPeer(const SLE_Addr_S *addr)
{
    return addr != NULL && addr->type == g_expectedAddressType &&
        memcmp(addr->addr, g_expectedPeer, IPOSL_LAYER2_ID_LEN) == 0;
}

static void FillProperty(NLSTK_SsapServicePropertyParam_S *property, const uint8_t uuid[16],
    const uint8_t *value, uint16_t valueLen, uint32_t operation)
{
    /* Demo UUIDs are private 128-bit UUIDs, so their SSAP item types must stay vendor-specific. */
    property->type = ITEM_TYPE_VENDOR_PROPERTY;
    SetUuid(&property->uuid, uuid);
    property->permission.permissionValue = SSAP_PERMISSION_AUTHENTICATION_NEED | SSAP_PERMISSION_ENCRYPTION_NEED |
        SSAP_PERMISSION_AUTHORIZATION_NEED;
    property->operation.operationValue = operation;
    property->val.data = (uint8_t *)value;
    property->val.len = valueLen;
}

static void OnReadPropertyAuthorize(int32_t appId, uint16_t requestId,
    NLSTK_SsapServerReadPropertyInfo_S *property)
{
    const IposlProfileCallbacks *callbacks = IposlGetCallbacks();
    IposlServerPeer *entry = property == NULL ? NULL : FindPeer(&property->addr);
    bool allow = appId == g_serverAppId && g_serverActive && property != NULL &&
        (g_anyPeer ? callbacks != NULL && callbacks->isSecureAddress != NULL &&
        callbacks->isSecureAddress(property->addr.addr, property->addr.type, g_generation) :
        IsExpectedPeer(&property->addr));
    if (allow && memcmp(property->uuid.uuid, g_gatewayStateUuid, sizeof(g_gatewayStateUuid)) == 0) {
        uint8_t state[IPOSL_GATEWAY_CAPABILITY_LEN];
        bool configured = g_anyPeer ? entry != NULL : g_configured;
        memcpy(state, configured ? g_iposlGatewayServing : g_iposlEmptyState,
            configured ? sizeof(state) : IPOSL_EMPTY_STATE_LEN);
        if (configured) state[11] = g_anyPeer ? entry->mode : g_selectedMode;
        NLSTK_VariableData_S value = {
            .len = (uint16_t)(configured ? sizeof(state) : IPOSL_EMPTY_STATE_LEN), .data = state
        };
        /* Both operations copy their data and enqueue on the same SSAP worker, update before read reply. */
        allow = NLSTK_SsapServerUpdatePropertyValue(appId, property->handle, &value) == NLSTK_ERRCODE_SUCCESS;
    }
    (void)NLSTK_SsapServerAuthorizeResult(appId, requestId, allow);
}

static int32_t AddIdentifierService(void)
{
    /* The SSAP allocator requires a non-null property array even when the service has no properties. */
    NLSTK_SsapServicePropertyParam_S emptyProperties = {0};
    NLSTK_ServiceParam_S service = {0};
    SetUuid(&service.serviceStatement.uuid, g_identifierUuid);
    service.serviceStatement.serviceType = ITEM_TYPE_VENDOR_PRIMARY_SERVICE;
    service.property = &emptyProperties;
    service.servicePropertyNum = 0;
    NLSTK_Errcode_E ret = NLSTK_SsapServerAddService(g_serverAppId, &service);
    if (ret != NLSTK_ERRCODE_SUCCESS) {
        NLSTK_LOG_ERROR("[IpShare][IPoSL][Server] identifier service add failed appId=%d ret=%d", g_serverAppId, ret);
        return IPOSL_ERR_SSAP;
    }
    NLSTK_LOG_INFO("[IpShare][IPoSL][Server] identifier service added appId=%d", g_serverAppId);
    return IPOSL_SUCCESS;
}

static int32_t AddConfigService(void)
{
    NLSTK_SsapServicePropertyParam_S properties[4] = {0};
    FillProperty(&properties[0], g_terminalCapabilityUuid, g_iposlTerminalCapability,
        IPOSL_TERMINAL_CAPABILITY_LEN, SSAP_OPERATE_INDICATION_READ);
    FillProperty(&properties[1], g_terminalStateUuid, g_iposlEmptyState,
        IPOSL_EMPTY_STATE_LEN, SSAP_OPERATE_INDICATION_READ | SSAP_OPERATE_INDICATION_NOTIFY);
    FillProperty(&properties[2], g_gatewayCapabilityUuid, g_iposlGatewayCapability,
        IPOSL_GATEWAY_CAPABILITY_LEN, SSAP_OPERATE_INDICATION_READ);
    FillProperty(&properties[3], g_gatewayStateUuid, g_iposlEmptyState,
        IPOSL_EMPTY_STATE_LEN, SSAP_OPERATE_INDICATION_READ | SSAP_OPERATE_INDICATION_NOTIFY);

    NLSTK_SsapServiceMethodParam_S method = {0};
    method.type = ITEM_TYPE_VENDOR_METHOD;
    SetUuid(&method.uuid, g_methodUuid);
    method.permission.permissionValue = SSAP_PERMISSION_AUTHENTICATION_NEED | SSAP_PERMISSION_ENCRYPTION_NEED;

    NLSTK_ServiceParam_S service = {0};
    SetUuid(&service.serviceStatement.uuid, g_configUuid);
    service.serviceStatement.serviceType = ITEM_TYPE_VENDOR_PRIMARY_SERVICE;
    service.servicePropertyNum = 4;
    service.property = properties;
    service.serviceMethodNum = 1;
    service.method = &method;
    NLSTK_Errcode_E ret = NLSTK_SsapServerAddService(g_serverAppId, &service);
    if (ret != NLSTK_ERRCODE_SUCCESS) {
        NLSTK_LOG_ERROR("[IpShare][IPoSL][Server] configuration service add failed appId=%d ret=%d", g_serverAppId,
            ret);
        return IPOSL_ERR_SSAP;
    }
    NLSTK_LOG_INFO("[IpShare][IPoSL][Server] configuration service added appId=%d", g_serverAppId);
    return IPOSL_SUCCESS;
}

static void OnCallMethodAny(int32_t appId, uint16_t requestId,
    NLSTK_SsapServerCallMethodRequestInfo_S *method, bool needReturn, bool needAuth)
{
    uint8_t opcode = 0, layer2[IPOSL_LAYER2_ID_LEN] = {0}, response[IPOSL_RESPONSE_LEN] = {0};
    uint8_t result = 0xff;
    int32_t decoded = method == NULL ? IPOSL_ERR_INVALID_PARAM :
        IposlCodecDecodeRequest(method->param.data, method->param.len, &opcode, layer2);
    const IposlProfileCallbacks *callbacks = IposlGetCallbacks();
    bool accepted = g_serverActive && appId == g_serverAppId && method != NULL && method->handle != 0 &&
        decoded == IPOSL_SUCCESS && callbacks != NULL &&
        callbacks->isSecureAddress != NULL &&
        callbacks->isSecureAddress(method->addr.addr, method->addr.type, g_generation) &&
        memcmp(layer2, method->addr.addr, IPOSL_LAYER2_ID_LEN) == 0;
    IposlServerPeer *entry = accepted ? FindPeer(&method->addr) : NULL;
    IposlServerPeer *candidate = NULL;
    uint8_t mode = accepted && opcode == IPOSL_OPCODE_CONFIGURE ? method->param.data[10] : 0;
    bool reserved = false;
    if (accepted && opcode == IPOSL_OPCODE_CONFIGURE &&
        (mode == 1 || (mode == 3 && g_allowedMode == 3))) {
        if (entry != NULL) {
            /* The cached SSAP entry outlives channel release and L3 cleanup.
             * Revalidate its owner before acknowledging a retransmission. */
            int32_t prepared = callbacks->prepareMode(method->addr.addr, mode, g_generation);
            result = prepared == 0 && entry->mode == mode ? 0 : 0xff;
        } else if ((candidate = FreePeer()) != NULL &&
            callbacks->prepareMode(method->addr.addr, mode, g_generation) == 0) {
            reserved = true;
            result = 0;
        }
    } else if (accepted && opcode == IPOSL_OPCODE_ENABLE && entry != NULL &&
        memcmp(entry->layer2, layer2, IPOSL_LAYER2_ID_LEN) == 0) {
        int32_t prepared = callbacks->prepareMode(method->addr.addr, entry->mode, g_generation);
        result = prepared == 0 ? 0 : 0xff;
    }
    if (accepted && opcode == IPOSL_OPCODE_CONFIGURE && mode == 3 && g_allowedMode != 3) {
        result = 0x07; // Explicit IP-type rejection, never capacity or identity rejection.
    }
    bool delivered = false;
    if (method != NULL && needReturn &&
        IposlCodecEncodeResponse(opcode, layer2, result, response, sizeof(response)) > 0) {
        NLSTK_VariableData_S value = {.len = sizeof(response), .data = response};
        delivered = NLSTK_SsapServerSendMethodCallRes(g_serverAppId, requestId, &value) == NLSTK_ERRCODE_SUCCESS;
    } else if (needAuth) {
        (void)NLSTK_SsapServerAuthorizeResult(g_serverAppId, requestId, accepted);
    }
    if (reserved && !delivered) (void)callbacks->prepareMode(method->addr.addr, 0, g_generation);
    if (!delivered || result != 0) return;
    if (reserved) {
        candidate->used = true;
        candidate->enabled = false;
        candidate->addressType = method->addr.type;
        candidate->mode = mode;
        memcpy(candidate->peer, method->addr.addr, IPOSL_LAYER2_ID_LEN);
        memcpy(candidate->layer2, layer2, IPOSL_LAYER2_ID_LEN);
        entry = candidate;
    }
    if (opcode == IPOSL_OPCODE_CONFIGURE && reserved) {
        callbacks->onConfigured(entry->peer, false, 0, entry->mode, g_generation);
    } else if (opcode == IPOSL_OPCODE_ENABLE && !entry->enabled) {
        entry->enabled = true;
        callbacks->onConfigured(entry->peer, true, 0, entry->mode, g_generation);
    }
}

static void OnCallMethod(int32_t appId, uint16_t requestId, NLSTK_SsapServerCallMethodRequestInfo_S *method,
    bool needReturn, bool needAuth)
{
    if (g_anyPeer) {
        OnCallMethodAny(appId, requestId, method, needReturn, needAuth);
        return;
    }
    uint8_t opcode = 0;
    uint8_t layer2[IPOSL_LAYER2_ID_LEN] = {0};
    uint8_t response[IPOSL_RESPONSE_LEN] = {0};
    uint8_t result = 0xFF;
    bool appMatch = appId == g_serverAppId;
    bool peerMatch = method != NULL && IsExpectedPeer(&method->addr);
    /* SSAP resolves the handle to this app's registered method before invoking this callback. The current
     * callback path does not populate method->uuid, and this private IPoSL app registers exactly one method. */
    bool methodHandleValid = method != NULL && method->handle != 0;
    int32_t decodeRet = method == NULL ? IPOSL_ERR_INVALID_PARAM :
        IposlCodecDecodeRequest(method->param.data, method->param.len, &opcode, layer2);
    bool accepted = g_serverActive && appMatch && method != NULL && peerMatch && methodHandleValid &&
        decodeRet == IPOSL_SUCCESS;
    if (!accepted) {
        NLSTK_LOG_ERROR("[IpShare][IPoSL][Server] method rejected requestId=%u appId=%d active=%d appMatch=%d "
            "method=%d handle=%u peerMatch=%d methodHandleValid=%d decodeRet=%d", requestId, appId, g_serverActive,
            appMatch, method != NULL, method == NULL ? 0 : method->handle, peerMatch, methodHandleValid, decodeRet);
    } else {
        NLSTK_LOG_INFO("[IpShare][IPoSL][Server] method request accepted requestId=%u appId=%d opcode=%u", requestId,
            appId, opcode);
    }
    const IposlProfileCallbacks *callbacks = IposlGetCallbacks();
    accepted = accepted && callbacks != NULL && callbacks->isSecure(g_expectedPeer, g_generation);
    uint8_t mode = opcode == IPOSL_OPCODE_CONFIGURE && decodeRet == 0 ? method->param.data[10] : 0;
    bool newReservation = false;
    if (accepted && opcode == IPOSL_OPCODE_CONFIGURE &&
        memcmp(layer2, g_expectedPeer, sizeof(g_expectedPeer)) == 0 &&
        (mode == 1 || (mode == 3 && g_allowedMode == 3))) {
        if (g_configured) {
            /* Compatible retransmission is idempotent; changing mode requires stop. */
            result = mode == g_selectedMode ? 0 : 0xff;
        } else if (callbacks->prepareMode(g_expectedPeer, mode, g_generation) == 0) {
            newReservation = true;
            result = 0;
        }
    } else if (accepted && opcode == IPOSL_OPCODE_ENABLE && g_configured &&
        memcmp(g_configuredLayer2, layer2, sizeof(g_configuredLayer2)) == 0) {
        result = 0;
    }
    if (accepted && opcode == IPOSL_OPCODE_CONFIGURE &&
        memcmp(layer2, g_expectedPeer, sizeof(g_expectedPeer)) == 0 && mode == 3 && g_allowedMode != 3) {
        result = 0x07;
    }
    bool delivered = false;
    if (method != NULL && IposlCodecEncodeResponse(opcode, layer2, result, response, sizeof(response)) > 0 &&
        needReturn) {
        NLSTK_VariableData_S value = {.len = sizeof(response), .data = response};
        delivered = NLSTK_SsapServerSendMethodCallRes(g_serverAppId, requestId, &value) == NLSTK_ERRCODE_SUCCESS;
    } else if (needAuth) {
        /* Authorization alone never commits a configuration or enables the user plane. */
        (void)NLSTK_SsapServerAuthorizeResult(g_serverAppId, requestId, accepted);
    }
    if (newReservation && !delivered) {
        (void)callbacks->prepareMode(g_expectedPeer, 0, g_generation);
    }
    if (!delivered || result != 0) return;
    if (newReservation) {
        memcpy(g_configuredLayer2, layer2, sizeof(g_configuredLayer2));
        g_selectedMode = mode;
        g_configured = true;
    }
    if (opcode == IPOSL_OPCODE_CONFIGURE && !g_enabled) {
        callbacks->onConfigured(g_expectedPeer, false, 0, g_selectedMode, g_generation);
    } else if (opcode == IPOSL_OPCODE_ENABLE && !g_enabled) {
        g_enabled = true;
        callbacks->onConfigured(g_expectedPeer, true, 0, g_selectedMode, g_generation);
    }
    NLSTK_LOG_INFO("[IpShare][IPoSL][Server] opcode=%u result=%u selectedMode=%u", opcode, result, g_selectedMode);
}

int32_t IposlServerInitialize(void)
{
    if (g_serverAppId != SSAP_APP_INVALID_ID) {
        NLSTK_LOG_INFO("[IpShare][IPoSL][Server] initialize skipped appId=%d", g_serverAppId);
        return IPOSL_SUCCESS;
    }
    NLSTK_SsapAppServerCb_S callbacks = {0};
    callbacks.onCallMethod = OnCallMethod;
    callbacks.onReadPropertyAuthorizeRequest = OnReadPropertyAuthorize;
    int32_t appId = SSAP_APP_INVALID_ID;
    NLSTK_Errcode_E registerRet = NLSTK_SsapServerRegApp(&callbacks, &appId);
    g_serverAppId = appId;
    if (registerRet != NLSTK_ERRCODE_SUCCESS || g_serverAppId == SSAP_APP_INVALID_ID) {
        g_serverAppId = SSAP_APP_INVALID_ID;
        NLSTK_LOG_ERROR("[IpShare][IPoSL][Server] register failed ret=%d", registerRet);
        return IPOSL_ERR_SSAP;
    }
    NLSTK_LOG_INFO("[IpShare][IPoSL][Server] registered appId=%d", g_serverAppId);
    if (AddIdentifierService() != IPOSL_SUCCESS || AddConfigService() != IPOSL_SUCCESS) {
        NLSTK_LOG_ERROR("[IpShare][IPoSL][Server] initialize failed while adding services appId=%d", g_serverAppId);
        IposlServerDeinit();
        return IPOSL_ERR_SSAP;
    }
    NLSTK_LOG_INFO("[IpShare][IPoSL][Server] initialize completed appId=%d", g_serverAppId);
    return IPOSL_SUCCESS;
}

static int32_t StartAnyOnCp(uint8_t mode, uint32_t capacity, uint64_t generation)
{
    const IposlProfileCallbacks *callbacks = IposlGetCallbacks();
    if (callbacks == NULL || callbacks->isSecureAddress == NULL ||
        g_serverAppId == SSAP_APP_INVALID_ID || g_serverActive ||
        (mode != 1 && mode != 3) || capacity == 0 || capacity > 32 || generation == 0) {
        return IPOSL_ERR_INVALID_PARAM;
    }
    g_peers = (IposlServerPeer *)calloc(capacity, sizeof(*g_peers));
    if (g_peers == NULL) return IPOSL_ERR_INVALID_STATE;
    g_peerCapacity = capacity;
    g_allowedMode = mode;
    g_generation = generation;
    g_anyPeer = true;
    g_serverActive = true;
    return IPOSL_SUCCESS;
}

static void ReleasePeerOnCp(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t addressType)
{
    if (peer == NULL || !g_anyPeer) return;
    SLE_Addr_S addr = {0};
    addr.type = addressType;
    memcpy(addr.addr, peer, IPOSL_LAYER2_ID_LEN);
    IposlServerPeer *entry = FindPeer(&addr);
    if (entry != NULL) memset(entry, 0, sizeof(*entry));
}

static int32_t StartOnCp(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t addressType,
    uint8_t mode, uint64_t generation)
{
    if (peer == NULL || g_serverAppId == SSAP_APP_INVALID_ID || g_serverActive) {
        NLSTK_LOG_ERROR("[IpShare][IPoSL][Server] start rejected peerNull=%d appId=%d active=%d", peer == NULL,
            g_serverAppId, g_serverActive);
        return IPOSL_ERR_INVALID_STATE;
    }
    (void)memcpy(g_expectedPeer, peer, sizeof(g_expectedPeer));
    g_expectedAddressType = addressType;
    g_configured = false;
    g_allowedMode = mode;
    g_selectedMode = 0;
    g_enabled = false;
    g_generation = generation;
    g_serverActive = true;
    NLSTK_LOG_INFO("[IpShare][IPoSL][Server] start completed appId=%d addressType=%u", g_serverAppId, addressType);
    return IPOSL_SUCCESS;
}

static void StopOnCp(void)
{
    bool wasActive = g_serverActive;
    (void)memset(g_expectedPeer, 0, sizeof(g_expectedPeer));
    (void)memset(g_configuredLayer2, 0, sizeof(g_configuredLayer2));
    g_expectedAddressType = 0;
    g_configured = false;
    g_serverActive = false;
    g_enabled = false;
    g_selectedMode = 0;
    g_generation = 0;
    free(g_peers);
    g_peers = NULL;
    g_peerCapacity = 0;
    g_anyPeer = false;
    NLSTK_LOG_INFO("[IpShare][IPoSL][Server] stop completed wasActive=%d", wasActive);
}

typedef enum {
    SERVER_START,
    SERVER_START_ANY,
    SERVER_RELEASE_PEER,
    SERVER_STOP
} IposlServerOperation;

typedef struct {
    atomic_int refs;
    atomic_bool cancelled;
    IposlServerOperation operation;
    uint8_t peer[IPOSL_LAYER2_ID_LEN];
    uint8_t addressType;
    uint8_t mode;
    uint32_t capacity;
    uint64_t generation;
    int32_t result;
} IposlServerTask;

static atomic_uint g_serverTasks;
static atomic_bool g_serverDrained = true;
static atomic_uint_fast64_t g_publishedGeneration;

static void ReleaseServerTask(void *arg)
{
    IposlServerTask *task = (IposlServerTask *)arg;
    if (atomic_fetch_sub(&task->refs, 1) == 1) {
        SDF_MemFree(task);
    }
}

static void FinishServerTask(void *arg)
{
    atomic_fetch_sub(&g_serverTasks, 1);
    ReleaseServerTask(arg);
}

static void RunServerTask(void *arg)
{
    IposlServerTask *task = (IposlServerTask *)arg;
    // A timed-out stop must still run: only CP may free the peer table.
    if ((task->operation == SERVER_START || task->operation == SERVER_START_ANY) &&
        atomic_load(&task->cancelled)) {
        return;
    }
    switch (task->operation) {
        case SERVER_START:
            task->result = StartOnCp(task->peer, task->addressType, task->mode, task->generation);
            break;
        case SERVER_START_ANY:
            task->result = StartAnyOnCp(task->mode, task->capacity, task->generation);
            break;
        case SERVER_RELEASE_PEER:
            if (task->generation == g_generation) {
                ReleasePeerOnCp(task->peer, task->addressType);
            }
            task->result = IPOSL_SUCCESS;
            break;
        case SERVER_STOP:
            StopOnCp();
            atomic_store(&g_serverDrained, true);
            task->result = IPOSL_SUCCESS;
            break;
    }
}

static int32_t PostServerTask(IposlServerOperation operation, const uint8_t *peer,
    uint8_t addressType, uint8_t mode, uint32_t capacity, uint64_t generation)
{
    if (operation == SERVER_START || operation == SERVER_START_ANY || operation == SERVER_STOP) {
        atomic_store(&g_serverDrained, false);
    }
    IposlServerTask *task = (IposlServerTask *)SDF_MemAlloc(sizeof(*task));
    if (task == NULL) {
        return IPOSL_ERR_INVALID_STATE;
    }
    atomic_init(&task->refs, 2);
    atomic_init(&task->cancelled, false);
    task->operation = operation;
    task->addressType = addressType;
    task->mode = mode;
    task->capacity = capacity;
    task->generation = generation;
    task->result = IPOSL_ERR_INVALID_STATE;
    if (peer != NULL) {
        memcpy(task->peer, peer, sizeof(task->peer));
    }
    atomic_fetch_add(&g_serverTasks, 1);
    uint32_t posted = CP_PostTaskBlocked(RunServerTask, task, FinishServerTask, 500);
    int32_t result = IPOSL_ERR_INVALID_STATE;
    if (posted == 0) {
        result = task->result;
    } else {
        atomic_store(&task->cancelled, true);
    }
    ReleaseServerTask(task);
    return result;
}

int32_t IposlServerStartAny(uint8_t mode, uint32_t capacity, uint64_t generation)
{
    atomic_store(&g_publishedGeneration, generation);
    return PostServerTask(SERVER_START_ANY, NULL, 0, mode, capacity, generation);
}

int32_t IposlServerStart(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t addressType,
    uint8_t mode, uint64_t generation)
{
    if (peer == NULL) {
        return IPOSL_ERR_INVALID_PARAM;
    }
    atomic_store(&g_publishedGeneration, generation);
    return PostServerTask(SERVER_START, peer, addressType, mode, 0, generation);
}

int32_t IposlServerReleasePeer(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t addressType)
{
    if (peer != NULL) {
        return PostServerTask(SERVER_RELEASE_PEER, peer, addressType, 0, 0,
            atomic_load(&g_publishedGeneration));
    }
    return IPOSL_ERR_INVALID_PARAM;
}

void IposlServerStop(void)
{
    (void)PostServerTask(SERVER_STOP, NULL, 0, 0, 0, 0);
}

bool IposlServerIsDrained(void)
{
    return atomic_load(&g_serverDrained) && atomic_load(&g_serverTasks) == 0;
}

void IposlServerDeinit(void)
{
    NLSTK_LOG_INFO("[IpShare][IPoSL][Server] deinit started appId=%d", g_serverAppId);
    IposlServerStop();
    if (g_serverAppId != SSAP_APP_INVALID_ID) {
        NLSTK_Errcode_E clearRet = NLSTK_SsapServerClearServices(g_serverAppId);
        if (clearRet != NLSTK_ERRCODE_SUCCESS) {
            NLSTK_LOG_ERROR("[IpShare][IPoSL][Server] clear services failed appId=%d ret=%d", g_serverAppId,
                clearRet);
        }
        NLSTK_SsapServerDeregisterApplication(g_serverAppId);
        NLSTK_LOG_INFO("[IpShare][IPoSL][Server] deregistered appId=%d", g_serverAppId);
    }
    g_serverAppId = SSAP_APP_INVALID_ID;
    NLSTK_LOG_INFO("[IpShare][IPoSL][Server] deinit completed");
}
