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
#include "nearlink_ipshare_client.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <mutex>

#include "i_nearlink_ipshare.h"
#include "nearlink_errorcode.h"
#include "nearlink_host.h"
#include "nearlink_ipshare_client_c.h"
#include "nearlink_ipshare_observer_stub.h"
#include "nearlink_sa_manager.h"
#include "nearlink_utils.h"
#include "log.h"

namespace OHOS::Nearlink {

namespace {
class ClientObserverStub final : public NearlinkIpShareObserverStub {
public:
    explicit ClientObserverStub(const std::shared_ptr<NearlinkIpShareObserver> &observer) : observer_(observer) {}

    void OnStatusChanged(const NearlinkIpShareStatus &status) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (status.generation < generation_ ||
                (status.generation == generation_ && status.sequence <= sequence_)) return;
            generation_ = status.generation;
            sequence_ = status.sequence;
        }
        auto observer = observer_.lock();
        if (observer != nullptr) {
            observer->OnStatusChanged(status);
        }
    }

private:
    std::weak_ptr<NearlinkIpShareObserver> observer_;
    std::mutex mutex_;
    uint64_t generation_ {0};
    uint64_t sequence_ {0};
};
}

struct NearlinkIpShareClient::impl {
    impl()
    {
        auto info = std::make_shared<NearlinkRegisterInfo>(PROFILE_IPSHARE_SERVER);
        profileRegisterId = NearlinkSaManager::GetInstance().RegisterFunc(info);
    }

    ~impl()
    {
        NearlinkSaManager::GetInstance().DeregisterFunc(profileRegisterId);
    }

    int32_t profileRegisterId {INVALID_PROFILE_ID};
    sptr<ClientObserverStub> observerStub;
};

NearlinkIpShareClient::NearlinkIpShareClient() : pimpl_(std::make_shared<impl>()) {}
NearlinkIpShareClient::~NearlinkIpShareClient() = default;

NearlinkIpShareClient &NearlinkIpShareClient::GetInstance()
{
    static NearlinkIpShareClient instance;
    return instance;
}

static sptr<INearlinkIpShare> GetIpShareProxy()
{
    return GetProxy<INearlinkIpShare>(PROFILE_IPSHARE_SERVER);
}

static int32_t ValidateClientAddress(const std::string &address)
{
    if (!NearlinkHost::GetInstance().IsNearlinkSupport()) {
        HILOGE("[IpShare][Client] request rejected: NearLink is not supported");
        return NL_ERR_API_NOT_SUPPORT;
    }
    if (!IS_SLE_ENABLED()) {
        HILOGE("[IpShare][Client] request rejected: SLE is disabled");
        return NL_ERR_SLE_OFF;
    }
    if (!IsValidAddress(address)) {
        HILOGE("[IpShare][Client] request rejected: invalid peer address");
        return NL_ERR_INVALID_PARAM;
    }
    return NL_NO_ERROR;
}

int32_t NearlinkIpShareClient::IsPeerSupported(const std::string &peerAddress, bool &supported) const
{
    HILOGI("[IpShare][Client] support probe requested");
    int32_t ret = ValidateClientAddress(peerAddress);
    if (ret != NL_NO_ERROR) {
        return ret;
    }
    auto proxy = GetIpShareProxy();
    if (proxy == nullptr) {
        HILOGE("[IpShare][Client] support probe failed: proxy unavailable");
        return NL_ERR_UNAVAILABLE_PROXY;
    }
    ret = proxy->IsPeerSupported(peerAddress, supported);
    HILOGI("[IpShare][Client] support probe finished ret=%{public}d supported=%{public}d", ret, supported);
    return ret;
}

int32_t NearlinkIpShareClient::StartGateway(const std::string &peerAddress) const
{
    HILOGI("[IpShare][Client] gateway start requested");
    int32_t ret = ValidateClientAddress(peerAddress);
    if (ret != NL_NO_ERROR) {
        return ret;
    }
    auto proxy = GetIpShareProxy();
    if (proxy == nullptr) {
        HILOGE("[IpShare][Client] gateway start failed: proxy unavailable");
        return NL_ERR_UNAVAILABLE_PROXY;
    }
    ret = proxy->StartGateway(peerAddress);
    HILOGI("[IpShare][Client] gateway start accepted ret=%{public}d", ret);
    return ret;
}

int32_t NearlinkIpShareClient::StartTerminal(const std::string &gatewayAddress) const
{
    HILOGI("[IpShare][Client] terminal start requested");
    int32_t ret = ValidateClientAddress(gatewayAddress);
    if (ret != NL_NO_ERROR) {
        return ret;
    }
    auto proxy = GetIpShareProxy();
    if (proxy == nullptr) {
        HILOGE("[IpShare][Client] terminal start failed: proxy unavailable");
        return NL_ERR_UNAVAILABLE_PROXY;
    }
    ret = proxy->StartTerminal(gatewayAddress);
    HILOGI("[IpShare][Client] terminal start accepted ret=%{public}d", ret);
    return ret;
}

int32_t NearlinkIpShareClient::Stop() const
{
    HILOGI("[IpShare][Client] stop requested");
    auto proxy = GetIpShareProxy();
    if (proxy == nullptr) {
        HILOGE("[IpShare][Client] stop failed: proxy unavailable");
        return NL_ERR_UNAVAILABLE_PROXY;
    }
    int32_t ret = proxy->Stop();
    HILOGI("[IpShare][Client] stop accepted ret=%{public}d", ret);
    return ret;
}

int32_t NearlinkIpShareClient::GetStatus(NearlinkIpShareStatus &status) const
{
    auto proxy = GetIpShareProxy();
    if (proxy == nullptr) {
        HILOGE("[IpShare][Client] status query failed: proxy unavailable");
        return NL_ERR_UNAVAILABLE_PROXY;
    }
    int32_t ret = proxy->GetStatus(status);
    if (ret != NL_NO_ERROR) {
        HILOGE("[IpShare][Client] status query failed ret=%{public}d", ret);
    } else {
        HILOGD("[IpShare][Client] status query ret=0 role=%{public}d state=%{public}d error=%{public}d",
            static_cast<int32_t>(status.role), static_cast<int32_t>(status.state), status.errorCode);
    }
    return ret;
}

int32_t NearlinkIpShareClient::RegisterObserver(const std::shared_ptr<NearlinkIpShareObserver> &observer) const
{
    if (observer == nullptr) {
        HILOGE("[IpShare][Client] observer registration failed: observer is null");
        return NL_ERR_INVALID_PARAM;
    }
    auto proxy = GetIpShareProxy();
    if (proxy == nullptr) {
        HILOGE("[IpShare][Client] observer registration failed: proxy unavailable");
        return NL_ERR_UNAVAILABLE_PROXY;
    }
    sptr<ClientObserverStub> observerStub = new (std::nothrow) ClientObserverStub(observer);
    if (observerStub == nullptr) {
        HILOGE("[IpShare][Client] observer registration failed: allocation failed");
        return NL_ERR_INTERNAL_ERROR;
    }
    int32_t ret = proxy->RegisterObserver(observerStub);
    if (ret == NL_NO_ERROR) {
        pimpl_->observerStub = observerStub;
        NearlinkIpShareStatus snapshot;
        if (proxy->GetStatus(snapshot) == 0) observerStub->OnStatusChanged(snapshot);
    }
    HILOGI("[IpShare][Client] observer registration ret=%{public}d", ret);
    return ret;
}

int32_t NearlinkIpShareClient::UnregisterObserver() const
{
    auto proxy = GetIpShareProxy();
    if (proxy == nullptr) {
        HILOGE("[IpShare][Client] observer unregistration failed: proxy unavailable");
        return NL_ERR_UNAVAILABLE_PROXY;
    }
    int32_t ret = proxy->UnregisterObserver();
    if (ret == NL_NO_ERROR) {
        pimpl_->observerStub = nullptr;
    }
    HILOGI("[IpShare][Client] observer unregistration ret=%{public}d", ret);
    return ret;
}

int32_t NearlinkIpShareClient::QueryNearlinkIpShareCapabilities(const std::string &peerAddress, NearlinkIpShareCapabilities &capabilities) const
{
    int32_t ret = ValidateClientAddress(peerAddress);
    if (ret != 0) return ret;
    auto proxy = GetIpShareProxy();
    return proxy == nullptr ? NL_ERR_UNAVAILABLE_PROXY : proxy->QueryNearlinkIpShareCapabilities(peerAddress, capabilities);
}

int32_t NearlinkIpShareClient::StartNearlinkGatewayWithMode(const std::string &peerAddress, int32_t mode) const
{
    int32_t ret = ValidateClientAddress(peerAddress);
    if (ret != 0) return ret;
    auto proxy = GetIpShareProxy();
    return proxy == nullptr ? NL_ERR_UNAVAILABLE_PROXY : proxy->StartNearlinkGatewayWithMode(peerAddress, mode);
}

int32_t NearlinkIpShareClient::StartNearlinkTerminalWithMode(const std::string &peerAddress, int32_t mode) const
{
    int32_t ret = ValidateClientAddress(peerAddress);
    if (ret != 0) return ret;
    auto proxy = GetIpShareProxy();
    return proxy == nullptr ? NL_ERR_UNAVAILABLE_PROXY : proxy->StartNearlinkTerminalWithMode(peerAddress, mode);
}

int32_t NearlinkIpShareClient::UpdateValidatedAddress(const NearlinkIpShareAddressEvidence &address) const
{
    auto proxy = GetIpShareProxy();
    return proxy == nullptr ? NL_ERR_UNAVAILABLE_PROXY : proxy->UpdateValidatedAddress(address);
}

}  // namespace OHOS::Nearlink

namespace {
template <size_t Size>
void CopyText(char (&destination)[Size], const std::string &source)
{
    (void)std::snprintf(destination, Size, "%s", source.c_str());
}
}

extern "C" int32_t NlIpShareIsPeerSupported(const char *peerAddress, int32_t *supported)
{
    if (peerAddress == nullptr || supported == nullptr) {
        HILOGE("[IpShare][Client] C support probe failed: null argument");
        return OHOS::Nearlink::NL_ERR_INVALID_PARAM;
    }
    bool value = false;
    int32_t ret = OHOS::Nearlink::NearlinkIpShareClient::GetInstance().IsPeerSupported(peerAddress, value);
    *supported = value ? 1 : 0;
    return ret;
}

extern "C" int32_t NlIpShareStartGateway(const char *peerAddress)
{
    if (peerAddress == nullptr) {
        HILOGE("[IpShare][Client] C gateway start failed: null peer");
        return OHOS::Nearlink::NL_ERR_INVALID_PARAM;
    }
    return OHOS::Nearlink::NearlinkIpShareClient::GetInstance().StartGateway(peerAddress);
}

extern "C" int32_t NlIpShareStartTerminal(const char *gatewayAddress)
{
    if (gatewayAddress == nullptr) {
        HILOGE("[IpShare][Client] C terminal start failed: null gateway");
        return OHOS::Nearlink::NL_ERR_INVALID_PARAM;
    }
    return OHOS::Nearlink::NearlinkIpShareClient::GetInstance().StartTerminal(gatewayAddress);
}

extern "C" int32_t NlIpShareStop(void)
{
    return OHOS::Nearlink::NearlinkIpShareClient::GetInstance().Stop();
}

extern "C" int32_t NlIpShareGetStatus(NlIpShareStatusC *status)
{
    if (status == nullptr) {
        HILOGE("[IpShare][Client] C status query failed: null status");
        return OHOS::Nearlink::NL_ERR_INVALID_PARAM;
    }
    OHOS::Nearlink::NearlinkIpShareStatus value;
    int32_t ret = OHOS::Nearlink::NearlinkIpShareClient::GetInstance().GetStatus(value);
    if (ret != OHOS::Nearlink::NL_NO_ERROR) {
        HILOGE("[IpShare][Client] C status query failed ret=%{public}d", ret);
        return ret;
    }
    *status = {};
    status->role = static_cast<int32_t>(value.role);
    status->state = static_cast<int32_t>(value.state);
    CopyText(status->peerAddress, value.peerAddress);
    CopyText(status->ifaceName, value.ifaceName);
    CopyText(status->ipv4Address, value.ipv4Address);
    status->hasUpstream = value.hasUpstream ? 1 : 0;
    CopyText(status->errorStage, value.errorStage);
    status->errorCode = value.errorCode;
    CopyText(status->contextId, value.contextId);
    status->generation = value.generation;
    status->sequence = value.sequence;
    status->requestedMode = static_cast<int32_t>(value.requestedMode);
    status->selectedMode = static_cast<int32_t>(value.selectedMode);
    status->serviceReady = value.serviceReady;
    return OHOS::Nearlink::NL_NO_ERROR;
}

extern "C" int32_t NlIpShareStartGatewayWithMode(const char *peerAddress, int32_t mode)
{
    if (peerAddress == nullptr || !OHOS::Nearlink::IsIpShareMode(mode)) return -1;
    return OHOS::Nearlink::NearlinkIpShareClient::GetInstance().StartNearlinkGatewayWithMode(peerAddress, mode);
}

extern "C" int32_t NlIpShareStartTerminalWithMode(const char *peerAddress, int32_t mode)
{
    if (peerAddress == nullptr || !OHOS::Nearlink::IsIpShareMode(mode)) return -1;
    return OHOS::Nearlink::NearlinkIpShareClient::GetInstance().StartNearlinkTerminalWithMode(peerAddress, mode);
}

extern "C" int32_t NlIpShareQueryCapabilities(const char *peerAddress, NlIpShareCapabilitiesC *capabilities)
{
    if (peerAddress == nullptr || capabilities == nullptr) return -1;
    OHOS::Nearlink::NearlinkIpShareCapabilities value;
    int32_t ret = OHOS::Nearlink::NearlinkIpShareClient::GetInstance().QueryNearlinkIpShareCapabilities(peerAddress, value);
    if (ret != 0) return ret;
    *capabilities = {};
    capabilities->identifierPresent = value.identifierPresent;
    capabilities->discoveryState = value.discoveryState;
    capabilities->localModeCount = value.localModes.size();
    capabilities->peerModeCount = value.peerModes.size();
    capabilities->peerCapabilityKnown = value.peerCapabilityKnown;
    for (size_t i = 0; i < value.localModes.size(); ++i) capabilities->localModes[i] = value.localModes[i];
    for (size_t i = 0; i < value.peerModes.size(); ++i) capabilities->peerModes[i] = value.peerModes[i];
    return 0;
}

extern "C" int32_t NlIpShareUpdateValidatedAddress(const NlIpShareIpv6AddressC *address)
{
    if (!address || strnlen(address->address, sizeof(address->address)) >= sizeof(address->address)) return -1;
    OHOS::Nearlink::NearlinkIpShareAddressEvidence value;
    value.generation = address->generation; value.sequence = address->sequence; value.address = address->address;
    value.ifindex = address->ifindex; value.prefixLength = address->prefixLength; value.flags = address->flags;
    value.preferredLifetime = address->preferredLifetime; value.validLifetime = address->validLifetime;
    return OHOS::Nearlink::NearlinkIpShareClient::GetInstance().UpdateValidatedAddress(value);
}
