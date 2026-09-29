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
#include "nearlink_ipshare_stub.h"

#include "ipc_types.h"
#include "log.h"
#include "nearlink_errorcode.h"
#include "nearlink_permission_manager.h"
#include "nearlink_service_ipc_interface_code.h"

namespace OHOS::Nearlink {

NearlinkIpShareStub::NearlinkIpShareStub()
{
    // The private profile has the same local authorization boundary as its system caller.
    auto permission = CHECK_PERM(true, MULTI_PERM(ACCESS_NEARLINK, CONNECTIVITY_INTERNAL));
    memberFuncMap_ = {
        {NL_IPSHARE_UPDATE_VALIDATED_ADDRESS, {UpdateValidatedAddressInner, permission}},
        {NL_IPSHARE_IS_PEER_SUPPORTED, {IsPeerSupportedInner, permission}},
        {NL_IPSHARE_START_GATEWAY, {StartGatewayInner, permission}},
        {NL_IPSHARE_START_TERMINAL, {StartTerminalInner, permission}},
        {NL_IPSHARE_STOP, {StopInner, permission}},
        {NL_IPSHARE_GET_STATUS, {GetStatusInner, permission}},
        {NL_IPSHARE_REGISTER_OBSERVER, {RegisterObserverInner, permission}},
        {NL_IPSHARE_UNREGISTER_OBSERVER, {UnregisterObserverInner, permission}},
        {NL_IPSHARE_QUERY_CAPABILITIES, {QueryNearlinkIpShareCapabilitiesInner, permission}},
        {NL_IPSHARE_START_GATEWAY_WITH_MODE, {StartNearlinkGatewayWithModeInner, permission}},
        {NL_IPSHARE_START_TERMINAL_WITH_MODE, {StartNearlinkTerminalWithModeInner, permission}},

    };
}

int32_t NearlinkIpShareStub::OnRemoteRequest(uint32_t code, MessageParcel &data, MessageParcel &reply,
    MessageOption &option)
{
    if (code != NL_IPSHARE_GET_STATUS) {
        HILOGI("[IpShare][IPC] server request received code=%{public}u", code);
    }
    CHECK_PERMISSION_AND_EXECUTE(NearlinkIpShareStub);
}

int32_t NearlinkIpShareStub::IsPeerSupportedInner(NearlinkIpShareStub *stub, MessageParcel &data,
    MessageParcel &reply)
{
    std::string address;
    if (!data.ReadString(address)) {
        HILOGE("[IpShare][IPC] support request rejected: address read failed");
        return TRANSACTION_ERR;
    }
    bool supported = false;
    int32_t ret = stub->IsPeerSupported(address, supported);
    if (!reply.WriteInt32(ret) || (ret == 0 && !reply.WriteBool(supported))) {
        HILOGE("[IpShare][IPC] support response write failed ret=%{public}d", ret);
        return TRANSACTION_ERR;
    }
    HILOGI("[IpShare][IPC] support request handled ret=%{public}d supported=%{public}d", ret, supported);
    return NO_ERROR;
}

int32_t NearlinkIpShareStub::StartGatewayInner(NearlinkIpShareStub *stub, MessageParcel &data,
    MessageParcel &reply)
{
    std::string address;
    if (!data.ReadString(address)) {
        HILOGE("[IpShare][IPC] gateway request rejected: address read failed");
        return TRANSACTION_ERR;
    }
    int32_t ret = stub->StartGateway(address);
    if (!reply.WriteInt32(ret)) {
        HILOGE("[IpShare][IPC] gateway response write failed ret=%{public}d", ret);
        return TRANSACTION_ERR;
    }
    HILOGI("[IpShare][IPC] gateway request handled ret=%{public}d", ret);
    return NO_ERROR;
}

int32_t NearlinkIpShareStub::StartTerminalInner(NearlinkIpShareStub *stub, MessageParcel &data,
    MessageParcel &reply)
{
    std::string address;
    if (!data.ReadString(address)) {
        HILOGE("[IpShare][IPC] terminal request rejected: address read failed");
        return TRANSACTION_ERR;
    }
    int32_t ret = stub->StartTerminal(address);
    if (!reply.WriteInt32(ret)) {
        HILOGE("[IpShare][IPC] terminal response write failed ret=%{public}d", ret);
        return TRANSACTION_ERR;
    }
    HILOGI("[IpShare][IPC] terminal request handled ret=%{public}d", ret);
    return NO_ERROR;
}

int32_t NearlinkIpShareStub::StopInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply)
{
    (void)data;
    int32_t ret = stub->Stop();
    if (!reply.WriteInt32(ret)) {
        HILOGE("[IpShare][IPC] stop response write failed ret=%{public}d", ret);
        return TRANSACTION_ERR;
    }
    HILOGI("[IpShare][IPC] stop request handled ret=%{public}d", ret);
    return NO_ERROR;
}

int32_t NearlinkIpShareStub::GetStatusInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply)
{
    (void)data;
    NearlinkIpShareStatus status;
    int32_t ret = stub->GetStatus(status);
    if (!reply.WriteInt32(ret) || (ret == 0 && !reply.WriteParcelable(&status))) {
        HILOGE("[IpShare][IPC] status response write failed ret=%{public}d", ret);
        return TRANSACTION_ERR;
    }
    if (ret != 0) {
        HILOGE("[IpShare][IPC] status request handled with failure ret=%{public}d", ret);
    } else {
        HILOGD("[IpShare][IPC] status request handled role=%{public}d state=%{public}d error=%{public}d",
            static_cast<int32_t>(status.role), static_cast<int32_t>(status.state), status.errorCode);
    }
    return NO_ERROR;
}

int32_t NearlinkIpShareStub::RegisterObserverInner(NearlinkIpShareStub *stub, MessageParcel &data,
    MessageParcel &reply)
{
    sptr<IRemoteObject> remote = data.ReadRemoteObject();
    sptr<INearlinkIpShareObserver> observer = iface_cast<INearlinkIpShareObserver>(remote);
    if (observer == nullptr) {
        HILOGE("[IpShare][IPC] observer registration rejected: observer is null");
        return TRANSACTION_ERR;
    }
    int32_t ret = stub->RegisterObserver(observer);
    if (!reply.WriteInt32(ret)) {
        HILOGE("[IpShare][IPC] observer registration response write failed ret=%{public}d", ret);
        return TRANSACTION_ERR;
    }
    HILOGI("[IpShare][IPC] observer registration handled ret=%{public}d", ret);
    return NO_ERROR;
}

int32_t NearlinkIpShareStub::UnregisterObserverInner(NearlinkIpShareStub *stub, MessageParcel &data,
    MessageParcel &reply)
{
    (void)data;
    int32_t ret = stub->UnregisterObserver();
    if (!reply.WriteInt32(ret)) {
        HILOGE("[IpShare][IPC] observer unregistration response write failed ret=%{public}d", ret);
        return TRANSACTION_ERR;
    }
    HILOGI("[IpShare][IPC] observer unregistration handled ret=%{public}d", ret);
    return NO_ERROR;
}

int32_t NearlinkIpShareStub::QueryNearlinkIpShareCapabilitiesInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply)
{
    std::string address;
    if (!data.ReadString(address) || address.size() != 17) return TRANSACTION_ERR;
    NearlinkIpShareCapabilities capabilities;
    int32_t ret = stub->QueryNearlinkIpShareCapabilities(address, capabilities);
    return reply.WriteInt32(ret) && (ret != 0 || reply.WriteParcelable(&capabilities)) ? NO_ERROR : TRANSACTION_ERR;
}

int32_t NearlinkIpShareStub::StartNearlinkGatewayWithModeInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply)
{
    std::string address;
    if (!data.ReadString(address) || address.size() != 17) return TRANSACTION_ERR;
    int32_t mode = 0;
    if (!data.ReadInt32(mode) || !IsIpShareMode(mode)) return TRANSACTION_ERR;
    return reply.WriteInt32(stub->StartNearlinkGatewayWithMode(address, mode)) ? NO_ERROR : TRANSACTION_ERR;
}

int32_t NearlinkIpShareStub::StartNearlinkTerminalWithModeInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply)
{
    std::string address;
    if (!data.ReadString(address) || address.size() != 17) return TRANSACTION_ERR;
    int32_t mode = 0;
    if (!data.ReadInt32(mode) || !IsIpShareMode(mode)) return TRANSACTION_ERR;
    return reply.WriteInt32(stub->StartNearlinkTerminalWithMode(address, mode)) ? NO_ERROR : TRANSACTION_ERR;
}

int32_t NearlinkIpShareStub::UpdateValidatedAddressInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply)
{
    NearlinkIpShareAddressEvidence address;
    if (data.GetReadableBytes() > 256 || !address.Read(data) || data.GetReadableBytes() != 0) return TRANSACTION_ERR;
    return reply.WriteInt32(stub->UpdateValidatedAddress(address)) ? NO_ERROR : TRANSACTION_ERR;
}

}  // namespace OHOS::Nearlink
