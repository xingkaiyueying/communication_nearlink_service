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
#include "nearlink_ipshare_service.h"

#include <array>
#include <chrono>
#include <cstring>

#include "SleProperties.h"
#include "SleRemoteDeviceAdapter.h"
#include "ThreadUtil.h"
#include "iposl_profile.h"
#include "log.h"
#include "nearlink_ipshare_channel.h"
#include "nearlink_utils.h"
#include "raw_address.h"
#include "parameters.h"

namespace OHOS::Nearlink {
namespace {
constexpr int32_t IP_SHARE_OK = 0;
constexpr int32_t IP_SHARE_INVALID_ARGUMENT = -1;
constexpr int32_t IP_SHARE_INVALID_STATE = -2;
constexpr int32_t IP_SHARE_LINK_NOT_SECURE = -3;
constexpr int32_t IP_SHARE_PROFILE_FAILED = -4;
constexpr int32_t IP_SHARE_RESOURCE_FAILED = -5;
constexpr auto SUPPORT_WAIT = std::chrono::seconds(30);
constexpr int32_t IP_SHARE_MAX_CAPACITY_LIMIT = 32;
constexpr const char *MAX_TERMINALS_PARAM = "persist.nearlink.ipshare.max_terminals";
} // namespace

int32_t NearlinkIpShareService::GetSupportedMaxTerminals() const
{
    int32_t configured = OHOS::system::GetIntParameter(MAX_TERMINALS_PARAM, 2);
    return configured > 0 && configured <= IP_SHARE_MAX_CAPACITY_LIMIT ? configured : 0;
}

NearlinkIpShareService::GatewayPeer *NearlinkIpShareService::FindGatewayPeerLocked(const uint8_t peer[6]) const
{
    for (const auto &entry : gatewayPeers_) {
        if (entry != nullptr && memcmp(entry->address.data(), peer, 6) == 0) return entry.get();
    }
    return nullptr;
}

NearlinkIpShareService &NearlinkIpShareService::GetInstance()
{
    static NearlinkIpShareService instance;
    return instance;
}

int32_t NearlinkIpShareService::Initialize()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        HILOGI("[IpShare][Service] initialize skipped: already initialized");
        return IP_SHARE_OK;
    }
    HILOGI("[IpShare][Service] initialize started");
    IposlProfileCallbacks callbacks = {
        .onPeerSupported = &NearlinkIpShareService::OnPeerSupported,
        .onConfigured = &NearlinkIpShareService::OnConfigured,
        .prepareMode = &NearlinkIpShareService::PrepareMode,
        .isSecure = &NearlinkIpShareService::IsSecure,
        .canSend = &NearlinkIpShareService::CanSend,
        .onPeerCapabilities = &NearlinkIpShareService::OnPeerCapabilities,
        .isSecureAddress = &NearlinkIpShareService::IsSecureAddress,
    };
    int32_t profileRet = IposlProfileInit(&callbacks);
    if (profileRet != IPOSL_SUCCESS) {
        HILOGE("[IpShare][Service] initialize failed at IPoSL profile ret=%{public}d", profileRet);
        return IP_SHARE_PROFILE_FAILED;
    }
    int32_t channelRet =
        NearlinkIpShareChannel::GetInstance().Initialize([this](bool established, int32_t error, uint64_t generation) {
            DoInIpShareThread([this, established, error, generation]() {
                NearlinkIpShareStatus snapshot;
                if (GetStatus(snapshot) == 0 && snapshot.generation == generation) {
                    HandleChannelState(established, error);
                }
            });
        });
    if (channelRet != 0) {
        HILOGE("[IpShare][Service] initialize failed at IPv4 channel ret=%{public}d", channelRet);
        IposlProfileDeinit();
        return IP_SHARE_PROFILE_FAILED;
    }
    status_ = {};
    if (generationCounter_ == 0) {
        generationCounter_ = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    status_.generation = ++generationCounter_;
    StampLocked();
    initialized_ = true;
    HILOGI("[IpShare][Service] initialize completed");
    return IP_SHARE_OK;
}

void NearlinkIpShareService::Shutdown()
{
    HILOGI("[IpShare][Service] shutdown started");
    StopNow();
    NearlinkIpShareChannel::GetInstance().Deinitialize();
    IposlProfileDeinit();
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
    observer_ = nullptr;
    HILOGI("[IpShare][Service] shutdown completed");
}

void NearlinkIpShareService::ResetForAdapterStop()
{
    HILOGI("[IpShare][Service] adapter reset started");
    StopNow();
    NearlinkIpShareChannel::GetInstance().Deinitialize();
    IposlProfileDeinit();
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
    HILOGI("[IpShare][Service] adapter reset completed");
}

int32_t NearlinkIpShareService::ValidateSecurePeer(const std::string &peerAddress, uint8_t peer[6],
                                                   uint8_t &addressType) const
{
    if (!IsValidAddress(peerAddress) || peer == nullptr) {
        HILOGE("[IpShare][Service] secure-peer validation failed: invalid argument");
        return IP_SHARE_INVALID_ARGUMENT;
    }
    RawAddress address(peerAddress);
    auto *adapter = SleRemoteDeviceAdapter::GetInstance();
    if (adapter == nullptr) {
        HILOGE("[IpShare][Service] secure-peer validation failed: remote-device adapter unavailable");
        return IP_SHARE_LINK_NOT_SECURE;
    }
    if (!adapter->IsBondedFromLocal(address)) {
        HILOGE("[IpShare][Service] secure-peer validation failed: peer is not bonded");
        return IP_SHARE_LINK_NOT_SECURE;
    }
    if (!adapter->IsAcbConnected(address)) {
        HILOGE("[IpShare][Service] secure-peer validation failed: ACB is disconnected");
        return IP_SHARE_LINK_NOT_SECURE;
    }
    if (!adapter->IsAcbEncrypted(address)) {
        HILOGE("[IpShare][Service] secure-peer validation failed: ACB is not encrypted");
        return IP_SHARE_LINK_NOT_SECURE;
    }
    address.ConvertToUint8(peer, 6);
    addressType = adapter->GetPeerDeviceAddrType(address);
    return IP_SHARE_OK;
}

int32_t NearlinkIpShareService::IsPeerSupported(const std::string &peerAddress, bool &supported)
{
    HILOGI("[IpShare][Service] support probe started");
    uint8_t peer[6] = {};
    uint8_t addressType = 0;
    uint64_t generation = 0;
    int32_t ret = ValidateSecurePeer(peerAddress, peer, addressType);
    if (ret != IP_SHARE_OK) {
        HILOGE("[IpShare][Service] support probe failed before discovery ret=%{public}d", ret);
        return ret;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    if (initialized_ && status_.role != NearlinkIpShareRole::NONE &&
        status_.state != NearlinkIpShareState::IDLE && !probeInProgress_ &&
        memcmp(peer_, peer, sizeof(peer_)) == 0) {
        // Discovery uses the same IPoSL client as an active session. Reuse only
        // evidence for this peer instead of interrupting the running role.
        supported = (peerSupported_ && memcmp(supportedPeer_, peer, sizeof(peer_)) == 0) ||
            IsIpShareMode(static_cast<int32_t>(status_.selectedMode));
        return IP_SHARE_OK;
    }
    if (!initialized_ || status_.role != NearlinkIpShareRole::NONE ||
        (status_.state != NearlinkIpShareState::IDLE && !probeInProgress_)) {
        HILOGE("[IpShare][Service] support probe rejected initialized=%{public}d role=%{public}d state=%{public}d",
               initialized_, static_cast<int32_t>(status_.role), static_cast<int32_t>(status_.state));
        return IP_SHARE_INVALID_STATE;
    }
    if (probeInProgress_) return IP_SHARE_INVALID_STATE;
    if (!probeInProgress_) {
        status_.generation = ++generationCounter_;
        status_.sequence = 0;
        capabilities_ = {};
        probeInProgress_ = true;
        peerSupported_ = false;
        (void)memcpy(peer_, peer, sizeof(peer_));
        addressType_ = addressType;
        status_.state = NearlinkIpShareState::DISCOVERING;
        status_.peerAddress = peerAddress;
        HILOGI("[IpShare][Service] support probe dispatched to IPoSL thread addressType=%{public}u", addressType);
        auto peerCopy = std::array<uint8_t, 6>{};
        (void)memcpy(peerCopy.data(), peer, peerCopy.size());
        generation = status_.generation;
        StampLocked();
        DoInIpShareThread([peerCopy, addressType, generation]() {
            if (!GetInstance().IsCurrent(generation)) return;
            int32_t ret = IposlProfileProbePeer(peerCopy.data(), addressType, 1, generation);
            if (ret != IPOSL_SUCCESS) {
                HILOGE("[IpShare][Service] support probe failed to start IPoSL discovery ret=%{public}d", ret);
                NearlinkIpShareService::GetInstance().HandlePeerSupported(peerCopy.data(), false,
                                                                          IP_SHARE_PROFILE_FAILED, 0, false, generation);
            }
        });
    }
    bool completed = probeCondition_.wait_for(lock, SUPPORT_WAIT, [this]() { return !probeInProgress_; });
    if (!completed) {
        HILOGE("[IpShare][Service] support probe timed out");
        probeInProgress_ = false;
        peerSupported_ = false;
        status_.state = NearlinkIpShareState::ERROR;
        status_.errorStage = "support-timeout";
        status_.errorCode = IP_SHARE_PROFILE_FAILED;
        StampLocked();
        NearlinkIpShareStatus status = status_;
        sptr<INearlinkIpShareObserver> observer = observer_;
        lock.unlock();
        DoInIpShareThread([]() { IposlProfileStopClient(); });
        NotifyStatus(status, observer);
        supported = false;
        return IP_SHARE_PROFILE_FAILED;
    }
    supported = completed && peerSupported_ && memcmp(supportedPeer_, peer, sizeof(supportedPeer_)) == 0;
    HILOGI("[IpShare][Service] support probe completed supported=%{public}d", supported);
    return IP_SHARE_OK;
}

int32_t NearlinkIpShareService::BeginRole(NearlinkIpShareRole role, const std::string &peerAddress,
                                          const uint8_t peer[6], uint8_t addressType, int32_t mode, uint64_t &generation)
{
    NearlinkIpShareStatus status;
    sptr<INearlinkIpShareObserver> observer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!IsIpShareMode(mode)) return IP_SHARE_INVALID_ARGUMENT;
        if (initialized_ && static_cast<int32_t>(status_.requestedMode) == mode && status_.role == role && memcmp(peer_, peer, sizeof(peer_)) == 0 &&
            addressType_ == addressType && status_.state != NearlinkIpShareState::STOPPING &&
            status_.state != NearlinkIpShareState::ERROR && status_.state != NearlinkIpShareState::IDLE) {
            return 1; // Already running: caller returns success without dispatching another start.
        }
        if (!initialized_ || probeInProgress_ || status_.state != NearlinkIpShareState::IDLE ||
            status_.role != NearlinkIpShareRole::NONE) {
            HILOGE("[IpShare][Service] role start rejected initialized=%{public}d probe=%{public}d role=%{public}d "
                   "state=%{public}d",
                   initialized_, probeInProgress_, static_cast<int32_t>(status_.role),
                   static_cast<int32_t>(status_.state));
            return IP_SHARE_INVALID_STATE;
        }
        SLE_Addr_S local = SleProperties::GetInstance().GetLocalSleAddress();
        bool gateway = role == NearlinkIpShareRole::GATEWAY;
        if (NearlinkIpShareChannel::GetInstance().SetPeer(peer, addressType, gateway, gateway ? peer : local.addr,
                local.addr, generationCounter_ + 1) !=
            0) {
            return IP_SHARE_INVALID_STATE; // Cancelled QoSM work is still draining; retry later.
        }
        status_ = {};
        status_.generation = ++generationCounter_;
        generation = status_.generation;
        status_.contextId = "ipshare-" + std::to_string(status_.generation);
        status_.requestedMode = static_cast<NearlinkIpShareMode>(mode);
        status_.role = role;
        status_.state = NearlinkIpShareState::STARTING;
        status_.peerAddress = peerAddress;
        status_.ifaceName = "sleip0";
        (void)memcpy(peer_, peer, sizeof(peer_));
        addressType_ = addressType;
        StampLocked();
        status = status_;
        observer = observer_;
    }
    HILOGI("[IpShare][Service] role start accepted role=%{public}d addressType=%{public}u", static_cast<int32_t>(role),
           addressType);
    NotifyStatus(status, observer);
    return IP_SHARE_OK;
}

int32_t NearlinkIpShareService::StartGatewayAny(int32_t mode, int32_t maxTerminals)
{
    const int32_t supported = GetSupportedMaxTerminals();
    if (!IsIpShareMode(mode) || supported == 0 || maxTerminals < 1 || maxTerminals > supported) {
        return IP_SHARE_INVALID_ARGUMENT;
    }
    uint64_t generation = 0;
    NearlinkIpShareStatus snapshot;
    sptr<INearlinkIpShareObserver> observer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (gatewayAny_ && status_.role == NearlinkIpShareRole::GATEWAY &&
            status_.state != NearlinkIpShareState::STOPPING && status_.state != NearlinkIpShareState::ERROR &&
            static_cast<int32_t>(status_.requestedMode) == mode && maxTerminals_ == maxTerminals) return IP_SHARE_OK;
        if (!initialized_ || probeInProgress_ || status_.role != NearlinkIpShareRole::NONE ||
            status_.state != NearlinkIpShareState::IDLE) return IP_SHARE_INVALID_STATE;
        gatewayPeers_.clear();
        gatewayPeers_.resize(static_cast<size_t>(maxTerminals));
        gatewayAny_ = true;
        maxTerminals_ = maxTerminals;
        status_ = {};
        status_.generation = ++generationCounter_;
        generation = status_.generation;
        status_.contextId = "ipshare-" + std::to_string(generation);
        status_.requestedMode = static_cast<NearlinkIpShareMode>(mode);
        status_.role = NearlinkIpShareRole::GATEWAY;
        status_.state = NearlinkIpShareState::STARTING;
        StampLocked();
        snapshot = status_;
        observer = observer_;
    }
    NotifyStatus(snapshot, observer);
    DoInIpShareThread([this, mode, maxTerminals, generation]() {
        if (!IsCurrent(generation)) return;
        if (IposlProfileStartServerAny(static_cast<uint8_t>(mode),
            static_cast<uint32_t>(maxTerminals), generation) != IPOSL_SUCCESS) {
            SetState(NearlinkIpShareState::ERROR, "gateway-listen", IP_SHARE_PROFILE_FAILED);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.serviceReady = true;
        }
        SetState(NearlinkIpShareState::SERVING_NO_UPSTREAM);
    });
    return IP_SHARE_OK;
}

int32_t NearlinkIpShareService::StartGateway(const std::string &peerAddress)
{
    return StartNearlinkGatewayWithMode(peerAddress, 1);
}

int32_t NearlinkIpShareService::StartNearlinkGatewayWithMode(const std::string &peerAddress, int32_t mode)
{
    HILOGI("[IpShare][Service] gateway start started");
    uint8_t peer[6] = {};
    uint8_t addressType = 0;
    uint64_t generation = 0;
    int32_t ret = ValidateSecurePeer(peerAddress, peer, addressType);
    if (ret != IP_SHARE_OK ||
        (ret = BeginRole(NearlinkIpShareRole::GATEWAY, peerAddress, peer, addressType, mode, generation)) != IP_SHARE_OK) {
        HILOGI("[IpShare][Service] gateway start result=%{public}d", ret);
        return ret == 1 ? IP_SHARE_OK : ret;
    }
    auto peerCopy = std::array<uint8_t, 6>{};
    (void)memcpy(peerCopy.data(), peer, peerCopy.size());
    DoInIpShareThread([this, peerCopy, addressType, mode, generation]() {
        if (!IsCurrent(generation)) return;
        int32_t serverRet = IposlProfileStartServer(peerCopy.data(), addressType, mode, generation);
        if (serverRet != IPOSL_SUCCESS) {
            HILOGE("[IpShare][Service] gateway start failed at IPoSL server ret=%{public}d", serverRet);
            IposlProfileStopServer();
            SetState(NearlinkIpShareState::ERROR, "gateway-start", IP_SHARE_RESOURCE_FAILED);
            return;
        }
        int32_t tunRet = NearlinkIpShareChannel::GetInstance().CreateTun();
        if (tunRet != 0) {
            HILOGE("[IpShare][Service] gateway start failed at TUN ret=%{public}d", tunRet);
            IposlProfileStopServer();
            SetState(NearlinkIpShareState::ERROR, "gateway-start", IP_SHARE_RESOURCE_FAILED);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.serviceReady = true;
        }
        HILOGI("[IpShare][Service] gateway IPoSL server and TUN are ready");
        SetState(NearlinkIpShareState::IFACE_READY);
    });
    HILOGI("[IpShare][Service] gateway start dispatched to IPoSL thread");
    return IP_SHARE_OK;
}

int32_t NearlinkIpShareService::StartTerminal(const std::string &gatewayAddress)
{
    return StartNearlinkTerminalWithMode(gatewayAddress, 1);
}

int32_t NearlinkIpShareService::StartNearlinkTerminalWithMode(const std::string &gatewayAddress, int32_t mode)
{
    HILOGI("[IpShare][Service] terminal start started");
    uint8_t peer[6] = {};
    uint8_t addressType = 0;
    uint64_t generation = 0;
    int32_t ret = ValidateSecurePeer(gatewayAddress, peer, addressType);
    if (ret != IP_SHARE_OK ||
        (ret = BeginRole(NearlinkIpShareRole::TERMINAL, gatewayAddress, peer, addressType, mode, generation)) != IP_SHARE_OK) {
        HILOGI("[IpShare][Service] terminal start result=%{public}d", ret);
        return ret == 1 ? IP_SHARE_OK : ret;
    }
    SLE_Addr_S local = SleProperties::GetInstance().GetLocalSleAddress();
    auto peerCopy = std::array<uint8_t, 6>{};
    auto localCopy = std::array<uint8_t, 6>{};
    (void)memcpy(peerCopy.data(), peer, peerCopy.size());
    (void)memcpy(localCopy.data(), local.addr, localCopy.size());
    DoInIpShareThread([peerCopy, localCopy, addressType, mode, generation]() {
        if (!GetInstance().IsCurrent(generation)) return;
        GetInstance().SetState(NearlinkIpShareState::DISCOVERING);
        int32_t ret = NearlinkIpShareChannel::GetInstance().CreateTun();
        if (ret == 0) ret = IposlProfileStartTerminal(peerCopy.data(), addressType, localCopy.data(), mode, generation);
        if (ret != IPOSL_SUCCESS) {
            HILOGE("[IpShare][Service] terminal start failed at IPoSL client ret=%{public}d", ret);
            NearlinkIpShareService::GetInstance().HandleConfigured(peerCopy.data(), false, IP_SHARE_PROFILE_FAILED, 0, generation);
        }
    });
    HILOGI("[IpShare][Service] terminal start dispatched; waiting for IPoSL callbacks");
    return IP_SHARE_OK;
}

int32_t NearlinkIpShareService::Stop()
{
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            HILOGE("[IpShare][Service] stop rejected: not initialized");
            return IP_SHARE_INVALID_STATE;
        }
        generation = status_.generation;
        status_.state = NearlinkIpShareState::STOPPING;
        StampLocked();
    }
    HILOGI("[IpShare][Service] stop dispatched");
    DoInIpShareThread([this, generation]() {
        NearlinkIpShareStatus snapshot;
        if (GetStatus(snapshot) == 0 && snapshot.generation == generation &&
            snapshot.state == NearlinkIpShareState::STOPPING) StopNow();
    });
    return IP_SHARE_OK;
}

void NearlinkIpShareService::StopNow()
{
    HILOGI("[IpShare][Service] stop cleanup started");
    IposlProfileStopClient();
    IposlProfileStopServer();
    for (auto &entry : gatewayPeers_) if (entry != nullptr) {
        entry->releasing = true;
        entry->channel->Close();
    }
    NearlinkIpShareChannel::GetInstance().Close();
    bool peersDrained = true;
    for (auto &entry : gatewayPeers_) if (entry != nullptr && !entry->channel->IsDrained()) peersDrained = false;
    if (!peersDrained || !NearlinkIpShareChannel::GetInstance().IsDrained()) {
        SetState(NearlinkIpShareState::STOPPING);
        return;
    }
    for (auto &entry : gatewayPeers_) if (entry != nullptr) entry->channel->Deinitialize();
    gatewayPeers_.clear();
    gatewayAny_ = false;
    maxTerminals_ = 0;
    NearlinkIpShareStatus status;
    sptr<INearlinkIpShareObserver> observer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t generation = status_.generation, sequence = status_.sequence;
        status_ = {};
        status_.generation = generation;
        status_.sequence = sequence;
        StampLocked();
        capabilities_ = {};
        probeInProgress_ = false;
        peerSupported_ = false;
        (void)memset(peer_, 0, sizeof(peer_));
        addressType_ = 0;
        status = status_;
        observer = observer_;
    }
    probeCondition_.notify_all();
    NotifyStatus(status, observer);
    HILOGI("[IpShare][Service] stop cleanup completed state=IDLE");
}

int32_t NearlinkIpShareService::GetStatus(NearlinkIpShareStatus &status) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        HILOGE("[IpShare][Service] status query rejected: not initialized");
        return IP_SHARE_INVALID_STATE;
    }
    status = status_;
    HILOGD("[IpShare][Service] status query role=%{public}d state=%{public}d error=%{public}d",
           static_cast<int32_t>(status.role), static_cast<int32_t>(status.state), status.errorCode);
    return IP_SHARE_OK;
}

int32_t NearlinkIpShareService::RegisterObserver(const sptr<INearlinkIpShareObserver> &observer)
{
    if (observer == nullptr) {
        HILOGE("[IpShare][Service] observer registration rejected: observer is null");
        return IP_SHARE_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    observer_ = observer;
    HILOGI("[IpShare][Service] observer registered");
    return IP_SHARE_OK;
}

int32_t NearlinkIpShareService::UnregisterObserver()
{
    std::lock_guard<std::mutex> lock(mutex_);
    observer_ = nullptr;
    HILOGI("[IpShare][Service] observer unregistered");
    return IP_SHARE_OK;
}

void NearlinkIpShareService::OnPeerSupported(const uint8_t peer[6], bool supported, int32_t error, uint8_t peerModes, bool known, uint64_t generation)
{
    if (peer == nullptr) {
        HILOGE("[IpShare][Service] support callback ignored: peer is null");
        return;
    }
    auto peerCopy = std::array<uint8_t, 6>{};
    (void)memcpy(peerCopy.data(), peer, peerCopy.size());
    DoInIpShareThread(
        [peerCopy, supported, error, peerModes, known, generation]() {
            GetInstance().HandlePeerSupported(peerCopy.data(), supported, error, peerModes, known, generation); });
    HILOGI("[IpShare][Service] support callback queued supported=%{public}d error=%{public}d", supported, error);
}

void NearlinkIpShareService::HandlePeerSupported(const uint8_t peer[6], bool supported, int32_t error, uint8_t peerModes, bool known, uint64_t generation)
{
    NearlinkIpShareStatus status;
    sptr<INearlinkIpShareObserver> observer;
    bool reportedSupported = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!probeInProgress_ || generation != status_.generation || memcmp(peer_, peer, sizeof(peer_)) != 0) {
            HILOGW("[IpShare][Service] support callback ignored: stale or no probe");
            return;
        }
        (void)memcpy(supportedPeer_, peer, sizeof(supportedPeer_));
        peerSupported_ = supported && error == 0;
        reportedSupported = peerSupported_;
        capabilities_.identifierPresent = peerSupported_;
        capabilities_.discoveryState = peerSupported_ ? 1 : 2;
        capabilities_.peerCapabilityKnown = known && peerSupported_;
        capabilities_.peerModes.clear();
        if (capabilities_.peerCapabilityKnown && (peerModes & 1)) capabilities_.peerModes.push_back(1);
        if (capabilities_.peerCapabilityKnown && peerModes == 3) capabilities_.peerModes.push_back(3);
        probeInProgress_ = false;
        status_.state = error == 0 ? NearlinkIpShareState::IDLE : NearlinkIpShareState::ERROR;
        status_.errorStage = error == 0 ? "" : "support";
        status_.errorCode = error;
        StampLocked();
        status = status_;
        observer = observer_;
    }
    probeCondition_.notify_all();
    NotifyStatus(status, observer);
    HILOGI("[IpShare][Service] support callback handled supported=%{public}d error=%{public}d", reportedSupported,
           error);
}

void NearlinkIpShareService::OnPeerCapabilities(const uint8_t peer[6], uint8_t peerModes, uint64_t generation)
{
    if (peer == nullptr) return;
    auto peerCopy = std::array<uint8_t, 6>{};
    (void)memcpy(peerCopy.data(), peer, peerCopy.size());
    DoInIpShareThread([peerCopy, peerModes, generation]() {
        GetInstance().HandlePeerCapabilities(peerCopy.data(), peerModes, generation); });
}

void NearlinkIpShareService::HandlePeerCapabilities(const uint8_t peer[6], uint8_t peerModes, uint64_t generation)
{
    if (peerModes != 0 && peerModes != 1 && peerModes != 3) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != status_.generation || status_.role != NearlinkIpShareRole::TERMINAL ||
        status_.state == NearlinkIpShareState::IDLE || status_.state == NearlinkIpShareState::STOPPING ||
        status_.state == NearlinkIpShareState::ERROR || memcmp(peer_, peer, sizeof(peer_)) != 0) return;
    capabilities_.identifierPresent = true;
    capabilities_.discoveryState = 1;
    capabilities_.peerCapabilityKnown = true;
    capabilities_.peerModes.clear();
    if (peerModes & 1) capabilities_.peerModes.push_back(1);
    if (peerModes == 3) capabilities_.peerModes.push_back(3);
    peerSupported_ = true;
    (void)memcpy(supportedPeer_, peer, sizeof(supportedPeer_));
}

void NearlinkIpShareService::OnConfigured(const uint8_t peer[6], bool opened, int32_t error, uint8_t mode, uint64_t generation)
{
    if (peer == nullptr) {
        HILOGE("[IpShare][Service] configuration callback ignored: peer is null");
        return;
    }
    auto peerCopy = std::array<uint8_t, 6>{};
    (void)memcpy(peerCopy.data(), peer, peerCopy.size());
    DoInIpShareThread([peerCopy, opened, error, mode, generation]() {
        GetInstance().HandleConfigured(peerCopy.data(), opened, error, mode, generation); });
    HILOGI("[IpShare][Service] configuration callback queued opened=%{public}d error=%{public}d", opened, error);
}

void NearlinkIpShareService::HandleConfigured(const uint8_t peer[6], bool opened, int32_t error, uint8_t mode, uint64_t generation)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (gatewayAny_ && generation == status_.generation && status_.role == NearlinkIpShareRole::GATEWAY) {
            auto *entry = FindGatewayPeerLocked(peer);
            if (entry == nullptr || entry->releasing) return;
            if (error != 0) {
                entry->releasing = true;
            } else {
                entry->configured = true;
                entry->selectedMode = mode;
            }
        } else if (gatewayAny_) return;
    }
    if (gatewayAny_) {
        if (error != 0) {
            ReleaseGatewayPeer(peer, 0);
            return;
        }
        if (!opened) return;
        NearlinkIpShareChannel *channel = nullptr;
        size_t slot = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (; slot < gatewayPeers_.size(); ++slot) {
                if (gatewayPeers_[slot] != nullptr &&
                    memcmp(gatewayPeers_[slot]->address.data(), peer, 6) == 0) {
                    channel = gatewayPeers_[slot]->channel.get();
                    break;
                }
            }
        }
        if (channel == nullptr || channel->EnableMode(mode) != 0 ||
            channel->CreateTun("sleip" + std::to_string(slot)) != 0) {
            ReleaseGatewayPeer(peer, 0);
        }
        return;
    }
    NearlinkIpShareRole role;
    std::array<uint8_t, 6> activePeer{};
    uint8_t addressType = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != status_.generation || status_.role == NearlinkIpShareRole::NONE ||
            memcmp(peer_, peer, sizeof(peer_)) != 0 || status_.state == NearlinkIpShareState::STOPPING ||
            status_.state == NearlinkIpShareState::ERROR) {
            HILOGW("[IpShare][Service] configuration callback ignored: stale peer or stopping");
            return;
        }
        role = status_.role;
        (void)memcpy(activePeer.data(), peer_, activePeer.size());
        addressType = addressType_;
    }
    if (error != 0) {
        HILOGE("[IpShare][Service] IPoSL configuration failed error=%{public}d", error);
        SetState(NearlinkIpShareState::ERROR, "iposl-config", error);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.selectedMode = static_cast<NearlinkIpShareMode>(mode);
    }
    if (opened && NearlinkIpShareChannel::GetInstance().EnableMode(mode) != 0) {
        SetState(NearlinkIpShareState::ERROR, "mode-enable", IP_SHARE_RESOURCE_FAILED);
        return;
    }
    SetState(NearlinkIpShareState::CONFIGURING);
    if (!opened) {
        HILOGI("[IpShare][Service] IPoSL configuration completed; awaiting enable response");
        return;
    }
    if (role == NearlinkIpShareRole::GATEWAY) {
        HILOGI("[IpShare][Service] gateway enable confirmed by peer");
        return;
    }
    int32_t tunRet = NearlinkIpShareChannel::GetInstance().CreateTun();
    if (tunRet != 0) {
        HILOGE("[IpShare][Service] terminal enable failed at TUN ret=%{public}d", tunRet);
        SetState(NearlinkIpShareState::ERROR, "tun", IP_SHARE_RESOURCE_FAILED);
        return;
    }
    SetState(NearlinkIpShareState::IFACE_READY);
    int32_t channelRet = NearlinkIpShareChannel::GetInstance().Open(activePeer.data(), addressType);
    if (channelRet != 0) {
        HILOGE("[IpShare][Service] terminal enable failed at QoSM channel ret=%{public}d", channelRet);
        SetState(NearlinkIpShareState::ERROR, "channel", IP_SHARE_RESOURCE_FAILED);
        return;
    }
    HILOGI("[IpShare][Service] terminal QoSM channel request submitted");
}

void NearlinkIpShareService::HandleChannelState(bool established, int32_t error)
{
    NearlinkIpShareState state;
    NearlinkIpShareRole role;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state = status_.state;
        role = status_.role;
    }
    if (state == NearlinkIpShareState::STOPPING && NearlinkIpShareChannel::GetInstance().IsDrained()) {
        StopNow();
        return;
    }
    if (state == NearlinkIpShareState::STOPPING || state == NearlinkIpShareState::IDLE ||
        state == NearlinkIpShareState::ERROR) {
        HILOGW("[IpShare][Service] channel callback ignored state=%{public}d", static_cast<int32_t>(state));
        return;
    }
    if (established) {
        HILOGI("[IpShare][Service] QoSM channel established");
        SetState(NearlinkIpShareState::CHANNEL_READY);
    } else if (error == 0 && role == NearlinkIpShareRole::GATEWAY) {
        // Peer departure ends its binding, not the explicitly started gateway or TUN.
        uint8_t peer[6], addressType;
        uint64_t generation;
        int32_t mode;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            generation = ++generationCounter_;
            status_.generation = generation;
            status_.sequence = 0;
            status_.contextId = "ipshare-" + std::to_string(generation);
            status_.selectedMode = NearlinkIpShareMode::NONE;
            mode = static_cast<int32_t>(status_.requestedMode);
            memcpy(peer, peer_, 6);
            addressType = addressType_;
        }
        IposlProfileStopServer();
        if (NearlinkIpShareChannel::GetInstance().ResetBinding(generation) != 0 ||
            IposlProfileStartServer(peer, addressType, mode, generation) != 0) {
            SetState(NearlinkIpShareState::ERROR, "binding-reset", IP_SHARE_RESOURCE_FAILED);
            return;
        }
        HILOGI("[IpShare][Service] peer released channel; gateway remains ready");
        SetState(NearlinkIpShareState::IFACE_READY);
    } else {
        if (error == 0)
            error = IP_SHARE_RESOURCE_FAILED;
        HILOGE("[IpShare][Service] QoSM channel failed error=%{public}d", error);
        SetState(NearlinkIpShareState::ERROR, "channel", error);
    }
}

void NearlinkIpShareService::SetState(NearlinkIpShareState state, const std::string &errorStage, int32_t error)
{
    if (state == NearlinkIpShareState::ERROR) {
        IposlProfileStopClient();
        IposlProfileStopServer();
        NearlinkIpShareChannel::GetInstance().Close();
    }
    NearlinkIpShareStatus status;
    sptr<INearlinkIpShareObserver> observer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state == NearlinkIpShareState::STOPPING && state != NearlinkIpShareState::STOPPING) return;
        status_.state = state;
        if (state == NearlinkIpShareState::ERROR) status_.serviceReady = false;
        status_.errorStage = errorStage;
        status_.errorCode = error;
        StampLocked();
        status = status_;
        observer = observer_;
    }
    HILOGI("[IpShare][Service] state transition role=%{public}d state=%{public}d stage=%{public}s error=%{public}d",
           static_cast<int32_t>(status.role), static_cast<int32_t>(state), errorStage.c_str(), error);
    NotifyStatus(status, observer);
}

void NearlinkIpShareService::StampLocked()
{
    ++status_.sequence;
}

bool NearlinkIpShareService::IsCurrent(uint64_t generation) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_ && status_.generation == generation &&
        status_.state != NearlinkIpShareState::STOPPING && status_.state != NearlinkIpShareState::IDLE &&
        status_.state != NearlinkIpShareState::ERROR;
}

void NearlinkIpShareService::HandleGatewayPeerChannel(const uint8_t peer[6], uint64_t epoch,
    bool established, int32_t error)
{
    bool stop = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto *entry = FindGatewayPeerLocked(peer);
        if (entry == nullptr || entry->epoch != epoch) return;
        if (status_.state == NearlinkIpShareState::STOPPING) stop = true;
        else if (!gatewayAny_ || status_.role != NearlinkIpShareRole::GATEWAY) return;
        else if (established) {
            entry->active = true;
            return;
        } else entry->releasing = true;
    }
    if (stop) StopNow();
    else ReleaseGatewayPeer(peer, epoch);
}

void NearlinkIpShareService::OnPeerDisconnected(const std::string &peerAddress)
{
    if (!IsValidAddress(peerAddress)) return;
    uint8_t peer[6] = {};
    RawAddress(peerAddress).ConvertToUint8(peer, 6);
    std::array<uint8_t, 6> address{};
    memcpy(address.data(), peer, address.size());
    DoInIpShareThread([address]() {
        auto &service = GetInstance();
        {
            std::lock_guard<std::mutex> lock(service.mutex_);
            if (!service.gatewayAny_ || service.FindGatewayPeerLocked(address.data()) == nullptr) return;
        }
        service.ReleaseGatewayPeer(address.data(), 0);
    });
}

void NearlinkIpShareService::ReleaseGatewayPeer(const uint8_t peer[6], uint64_t epoch)
{
    std::shared_ptr<NearlinkIpShareChannel> channel;
    uint64_t retiringEpoch = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto *entry = FindGatewayPeerLocked(peer);
        if (entry == nullptr || (epoch != 0 && entry->epoch != epoch)) return;
        entry->releasing = true; // Occupies its seat until QoSM and TUN have drained.
        retiringEpoch = entry->epoch;
        channel = entry->channel;
    }
    channel->Close(); // TUN reader joins outside the service lock.
    if (!channel->IsDrained()) return;
    std::unique_ptr<GatewayPeer> retired;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &entry : gatewayPeers_) {
            if (entry != nullptr && entry->epoch == retiringEpoch && entry->channel == channel) {
                retired = std::move(entry);
                break;
            }
        }
    }
    if (retired == nullptr) return;
    retired->channel->Deinitialize();
    IposlProfileReleaseServerPeer(peer, retired->addressType);
}

bool NearlinkIpShareService::IsSecure(const uint8_t peer[6], uint64_t generation)
{
    auto &service = GetInstance();
    std::string address;
    {
        std::lock_guard<std::mutex> lock(service.mutex_);
        if (!service.initialized_ || service.status_.generation != generation ||
            service.status_.state == NearlinkIpShareState::STOPPING ||
            service.status_.state == NearlinkIpShareState::IDLE ||
            service.status_.state == NearlinkIpShareState::ERROR) return false;
        if (!service.gatewayAny_ && memcmp(service.peer_, peer, 6) != 0) return false;
        address = service.gatewayAny_ ? RawAddress::ConvertToString(peer).GetAddress() : service.status_.peerAddress;
    }
    uint8_t verified[6], type = 0;
    return service.ValidateSecurePeer(address, verified, type) == 0 && memcmp(verified, peer, 6) == 0;
}

bool NearlinkIpShareService::IsSecureAddress(const uint8_t peer[6], uint8_t addressType, uint64_t generation)
{
    if (!IsSecure(peer, generation)) return false;
    uint8_t verified[6] = {}, type = 0;
    auto &service = GetInstance();
    return service.ValidateSecurePeer(RawAddress::ConvertToString(peer).GetAddress(), verified, type) == 0 &&
        type == addressType && memcmp(verified, peer, 6) == 0;
}

int32_t NearlinkIpShareService::PrepareMode(const uint8_t peer[6], uint8_t mode, uint64_t generation)
{
    auto &service = GetInstance();
    if (mode != 0 && !IsSecure(peer, generation)) return IP_SHARE_LINK_NOT_SECURE;
    if (service.gatewayAny_ && mode == 0) {
        {
            std::lock_guard<std::mutex> lock(service.mutex_);
            auto *entry = service.FindGatewayPeerLocked(peer);
            if (generation != service.status_.generation || entry == nullptr || entry->configured) return -1;
        }
        service.ReleaseGatewayPeer(peer, 0);
        return 0;
    }
    if (service.gatewayAny_) {
        std::lock_guard<std::mutex> lock(service.mutex_);
        if (generation != service.status_.generation ||
            service.status_.state == NearlinkIpShareState::STOPPING ||
            (mode != 0 && mode != 1 && mode != 3) ||
            (mode == 3 && service.status_.requestedMode != NearlinkIpShareMode::DUAL_STACK)) return -1;
        auto *existing = service.FindGatewayPeerLocked(peer);
        if (existing != nullptr) return existing->selectedMode == mode ? 0 : -1;
        size_t slot = 0;
        while (slot < service.gatewayPeers_.size() && service.gatewayPeers_[slot] != nullptr) ++slot;
        if (slot == service.gatewayPeers_.size()) return -1;
        uint8_t verified[6] = {}, type = 0;
        if (service.ValidateSecurePeer(RawAddress::ConvertToString(peer).GetAddress(), verified, type) != 0 ||
            memcmp(verified, peer, 6) != 0) return -1;
        auto entry = std::make_unique<GatewayPeer>();
        entry->address = {peer[0], peer[1], peer[2], peer[3], peer[4], peer[5]};
        entry->addressType = type;
        entry->epoch = ++service.generationCounter_;
        auto epoch = entry->epoch;
        auto address = entry->address;
        entry->channel = std::make_shared<NearlinkIpShareChannel>();
        if (entry->channel->Initialize([address, epoch](bool ready, int32_t error, uint64_t reported) {
            if (reported != epoch) return;
            DoInIpShareThread([address, epoch, ready, error]() {
                NearlinkIpShareService::GetInstance().HandleGatewayPeerChannel(address.data(), epoch, ready, error);
            });
        }) != 0) return -1;
        SLE_Addr_S local = SleProperties::GetInstance().GetLocalSleAddress();
        if (entry->channel->SetPeer(peer, type, true, peer, local.addr, epoch) != 0 ||
            entry->channel->PrepareMode(mode) != 0) {
            entry->channel->Deinitialize();
            return -1;
        }
        entry->selectedMode = mode;
        service.gatewayPeers_[slot] = std::move(entry);
        return 0;
    }
    std::lock_guard<std::mutex> lock(service.mutex_);
    if (generation != service.status_.generation || service.status_.state == NearlinkIpShareState::STOPPING ||
        (mode != 0 && mode != 1 && mode != 3) ||
        (mode == 3 && service.status_.requestedMode != NearlinkIpShareMode::DUAL_STACK)) return -1;
    return NearlinkIpShareChannel::GetInstance().PrepareMode(mode);
}

bool NearlinkIpShareService::CanSend(uint16_t lcid, uint8_t tcid, uint8_t pi, uint64_t generation)
{
    auto &service = GetInstance();
    uint8_t peer[6];
    uint64_t serverGeneration = 0;
    NearlinkIpShareChannel *channel = nullptr;
    {
        std::lock_guard<std::mutex> lock(service.mutex_);
        if (service.gatewayAny_) {
            for (const auto &entry : service.gatewayPeers_) {
                if (entry != nullptr && entry->epoch == generation && !entry->releasing &&
                    entry->channel->CanSend(lcid, tcid, pi, generation)) {
                    memcpy(peer, entry->address.data(), 6);
                    channel = entry->channel.get();
                    serverGeneration = service.status_.generation;
                    break;
                }
            }
        } else {
            memcpy(peer, service.peer_, 6);
            channel = &NearlinkIpShareChannel::GetInstance();
            serverGeneration = generation;
        }
    }
    return channel != nullptr && IsSecure(peer, serverGeneration) && channel->CanSend(lcid, tcid, pi, generation);
}

int32_t NearlinkIpShareService::QueryNearlinkIpShareCapabilities(const std::string &peerAddress,
    NearlinkIpShareCapabilities &capabilities)
{
    bool supported = false;
    int32_t ret = IsPeerSupported(peerAddress, supported);
    if (ret != 0) return ret;
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.peerAddress != peerAddress || probeInProgress_) return IP_SHARE_INVALID_STATE;
    capabilities = capabilities_;
    if (supported) {
        capabilities.identifierPresent = true;
        capabilities.discoveryState = 1;
    }
    // A selected mode proves this session works, but does not reveal every
    // mode advertised by the peer. Preserve peerCapabilityKnown as discovered.
    return 0;
}


void NearlinkIpShareService::NotifyStatus(const NearlinkIpShareStatus &status,
                                          const sptr<INearlinkIpShareObserver> &observer) const
{
    HILOGI("[IpShare][Status] generation=%{public}llu sequence=%{public}llu requestedMode=%{public}d selectedMode=%{public}d ready=%{public}d",
        static_cast<unsigned long long>(status.generation), static_cast<unsigned long long>(status.sequence),
        static_cast<int32_t>(status.requestedMode), static_cast<int32_t>(status.selectedMode), status.serviceReady);
    if (observer != nullptr) {
        observer->OnStatusChanged(status);
    } else {
        HILOGD("[IpShare][Service] status observer not registered");
    }
}

int32_t NearlinkIpShareService::UpdateValidatedAddress(const NearlinkIpShareAddressEvidence &address)
{
    return NearlinkIpShareChannel::GetInstance().UpdateValidatedAddress(address);
}
} // namespace OHOS::Nearlink
