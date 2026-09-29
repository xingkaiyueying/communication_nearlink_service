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
#include "nearlink_ipshare_proxy.h"

#include <memory>

#include "ipc_types.h"
#include "log.h"
#include "nearlink_errorcode.h"
#include "nearlink_service_ipc_interface_code.h"

namespace OHOS::Nearlink {

NearlinkIpShareProxy::NearlinkIpShareProxy(const sptr<IRemoteObject> &impl)
    : IRemoteProxy<INearlinkIpShare>(impl)
{}

int32_t NearlinkIpShareProxy::Transact(uint32_t code, MessageParcel &data, MessageParcel &reply)
{
    auto remote = Remote();
    if (remote == nullptr) {
        HILOGE("[IpShare][IPC] transact failed: remote is null code=%{public}u", code);
        return NL_ERR_UNAVAILABLE_PROXY;
    }
    MessageOption option(MessageOption::TF_SYNC);
    int32_t ret = remote->SendRequest(code, data, reply, option) == NO_ERROR ? NL_NO_ERROR : NL_ERR_IPC_TRANS_FAILED;
    if (ret != NL_NO_ERROR) {
        HILOGE("[IpShare][IPC] transact failed code=%{public}u ret=%{public}d", code, ret);
    }
    return ret;
}

int32_t NearlinkIpShareProxy::AddressCommand(uint32_t code, const std::string &address)
{
    MessageParcel data;
    MessageParcel reply;
    if (!data.WriteInterfaceToken(GetDescriptor()) || !data.WriteString(address)) {
        HILOGE("[IpShare][IPC] address request marshal failed code=%{public}u", code);
        return NL_ERR_IPC_TRANS_FAILED;
    }
    int32_t ret = Transact(code, data, reply);
    ret = ret == NL_NO_ERROR ? reply.ReadInt32() : ret;
    HILOGI("[IpShare][IPC] address request completed code=%{public}u ret=%{public}d", code, ret);
    return ret;
}

int32_t NearlinkIpShareProxy::IsPeerSupported(const std::string &peerAddress, bool &supported)
{
    MessageParcel data;
    MessageParcel reply;
    if (!data.WriteInterfaceToken(GetDescriptor()) || !data.WriteString(peerAddress)) {
        HILOGE("[IpShare][IPC] support request marshal failed");
        return NL_ERR_IPC_TRANS_FAILED;
    }
    int32_t ret = Transact(NL_IPSHARE_IS_PEER_SUPPORTED, data, reply);
    if (ret != NL_NO_ERROR) {
        return ret;
    }
    ret = reply.ReadInt32();
    if (ret == NL_NO_ERROR && !reply.ReadBool(supported)) {
        HILOGE("[IpShare][IPC] support response unmarshal failed");
        return NL_ERR_IPC_TRANS_FAILED;
    }
    HILOGI("[IpShare][IPC] support request completed ret=%{public}d supported=%{public}d", ret, supported);
    return ret;
}

int32_t NearlinkIpShareProxy::StartGateway(const std::string &peerAddress)
{
    return AddressCommand(NL_IPSHARE_START_GATEWAY, peerAddress);
}

int32_t NearlinkIpShareProxy::StartTerminal(const std::string &gatewayAddress)
{
    return AddressCommand(NL_IPSHARE_START_TERMINAL, gatewayAddress);
}

int32_t NearlinkIpShareProxy::Stop()
{
    MessageParcel data;
    MessageParcel reply;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        HILOGE("[IpShare][IPC] stop request marshal failed");
        return NL_ERR_IPC_TRANS_FAILED;
    }
    int32_t ret = Transact(NL_IPSHARE_STOP, data, reply);
    ret = ret == NL_NO_ERROR ? reply.ReadInt32() : ret;
    HILOGI("[IpShare][IPC] stop request completed ret=%{public}d", ret);
    return ret;
}

int32_t NearlinkIpShareProxy::GetStatus(NearlinkIpShareStatus &status)
{
    MessageParcel data;
    MessageParcel reply;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        HILOGE("[IpShare][IPC] status request marshal failed");
        return NL_ERR_IPC_TRANS_FAILED;
    }
    int32_t ret = Transact(NL_IPSHARE_GET_STATUS, data, reply);
    if (ret != NL_NO_ERROR) {
        HILOGE("[IpShare][IPC] status request failed ret=%{public}d", ret);
        return ret;
    }
    ret = reply.ReadInt32();
    if (ret != NL_NO_ERROR) {
        HILOGE("[IpShare][IPC] status response failed ret=%{public}d", ret);
        return ret;
    }
    std::shared_ptr<NearlinkIpShareStatus> result(reply.ReadParcelable<NearlinkIpShareStatus>());
    if (result == nullptr) {
        HILOGE("[IpShare][IPC] status response unmarshal failed");
        return NL_ERR_IPC_TRANS_FAILED;
    }
    status = *result;
    HILOGD("[IpShare][IPC] status completed role=%{public}d state=%{public}d error=%{public}d",
        static_cast<int32_t>(status.role), static_cast<int32_t>(status.state), status.errorCode);
    return NL_NO_ERROR;
}

int32_t NearlinkIpShareProxy::RegisterObserver(const sptr<INearlinkIpShareObserver> &observer)
{
    if (observer == nullptr) {
        HILOGE("[IpShare][IPC] observer registration failed: observer is null");
        return NL_ERR_INVALID_PARAM;
    }
    MessageParcel data;
    MessageParcel reply;
    if (!data.WriteInterfaceToken(GetDescriptor()) || !data.WriteRemoteObject(observer->AsObject())) {
        HILOGE("[IpShare][IPC] observer registration marshal failed");
        return NL_ERR_IPC_TRANS_FAILED;
    }
    int32_t ret = Transact(NL_IPSHARE_REGISTER_OBSERVER, data, reply);
    ret = ret == NL_NO_ERROR ? reply.ReadInt32() : ret;
    HILOGI("[IpShare][IPC] observer registration completed ret=%{public}d", ret);
    return ret;
}

int32_t NearlinkIpShareProxy::UnregisterObserver()
{
    MessageParcel data;
    MessageParcel reply;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        HILOGE("[IpShare][IPC] observer unregistration marshal failed");
        return NL_ERR_IPC_TRANS_FAILED;
    }
    int32_t ret = Transact(NL_IPSHARE_UNREGISTER_OBSERVER, data, reply);
    ret = ret == NL_NO_ERROR ? reply.ReadInt32() : ret;
    HILOGI("[IpShare][IPC] observer unregistration completed ret=%{public}d", ret);
    return ret;
}

int32_t NearlinkIpShareProxy::QueryNearlinkIpShareCapabilities(const std::string &peerAddress, NearlinkIpShareCapabilities &capabilities)
{
    MessageParcel data, reply;
    if (peerAddress.size() != 17) return NL_ERR_INVALID_PARAM;
    if (!data.WriteInterfaceToken(GetDescriptor()) || !data.WriteString(peerAddress))
        return NL_ERR_IPC_TRANS_FAILED;
    int32_t ret = Transact(NL_IPSHARE_QUERY_CAPABILITIES, data, reply);
    if (ret != NL_NO_ERROR) return ret;
    if (!reply.ReadInt32(ret)) return NL_ERR_IPC_TRANS_FAILED;
    if (ret != 0) return ret;
    std::unique_ptr<NearlinkIpShareCapabilities> value(reply.ReadParcelable<NearlinkIpShareCapabilities>());
    if (!value) return NL_ERR_IPC_TRANS_FAILED;
    capabilities = *value;
    return ret;
}

int32_t NearlinkIpShareProxy::StartNearlinkGatewayWithMode(const std::string &peerAddress, int32_t mode)
{
    MessageParcel data, reply;
    if (peerAddress.size() != 17 || !IsIpShareMode(mode)) return NL_ERR_INVALID_PARAM;
    if (!data.WriteInterfaceToken(GetDescriptor()) || !data.WriteString(peerAddress) || !data.WriteInt32(mode))
        return NL_ERR_IPC_TRANS_FAILED;
    int32_t ret = Transact(NL_IPSHARE_START_GATEWAY_WITH_MODE, data, reply);
    if (ret != NL_NO_ERROR) return ret;
    if (!reply.ReadInt32(ret)) return NL_ERR_IPC_TRANS_FAILED;
    return ret;
}

int32_t NearlinkIpShareProxy::StartNearlinkTerminalWithMode(const std::string &peerAddress, int32_t mode)
{
    MessageParcel data, reply;
    if (peerAddress.size() != 17 || !IsIpShareMode(mode)) return NL_ERR_INVALID_PARAM;
    if (!data.WriteInterfaceToken(GetDescriptor()) || !data.WriteString(peerAddress) || !data.WriteInt32(mode))
        return NL_ERR_IPC_TRANS_FAILED;
    int32_t ret = Transact(NL_IPSHARE_START_TERMINAL_WITH_MODE, data, reply);
    if (ret != NL_NO_ERROR) return ret;
    if (!reply.ReadInt32(ret)) return NL_ERR_IPC_TRANS_FAILED;
    return ret;
}

int32_t NearlinkIpShareProxy::UpdateValidatedAddress(const NearlinkIpShareAddressEvidence &address)
{
    MessageParcel data, reply;
    if (!data.WriteInterfaceToken(GetDescriptor()) || !address.Write(data)) return NL_ERR_IPC_TRANS_FAILED;
    int32_t ret = Transact(NL_IPSHARE_UPDATE_VALIDATED_ADDRESS, data, reply);
    if (ret != 0) return ret;
    return reply.ReadInt32(ret) ? ret : NL_ERR_IPC_TRANS_FAILED;
}

}  // namespace OHOS::Nearlink
