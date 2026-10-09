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
#ifndef NEARLINK_IPSHARE_SERVICE_H
#define NEARLINK_IPSHARE_SERVICE_H

#include <array>
#include <condition_variable>
#include <memory>
#include <vector>
#include <mutex>
#include <string>

#include "i_nearlink_ipshare_observer.h"
#include "nearlink_ipshare_status.h"

namespace OHOS::Nearlink {
class NearlinkIpShareChannel;

class NearlinkIpShareService final {
public:
    static NearlinkIpShareService &GetInstance();

    int32_t Initialize();
    void Shutdown();
    void ResetForAdapterStop();
    int32_t UpdateValidatedAddress(const NearlinkIpShareAddressEvidence &address);
    int32_t IsPeerSupported(const std::string &peerAddress, bool &supported);
    int32_t StartGateway(const std::string &peerAddress);
    int32_t StartGatewayAny(int32_t mode, int32_t maxTerminals);
    int32_t GetSupportedMaxTerminals() const;
    int32_t CompleteGatewayPeerRelease(uint64_t generation);
    void OnPeerDisconnected(const std::string &peerAddress);
    int32_t StartTerminal(const std::string &gatewayAddress);
    int32_t Stop();
    int32_t GetStatus(NearlinkIpShareStatus &status) const;
    int32_t RegisterObserver(const sptr<INearlinkIpShareObserver> &observer);
    int32_t UnregisterObserver();
    int32_t QueryNearlinkIpShareCapabilities(const std::string &peerAddress, NearlinkIpShareCapabilities &capabilities);
    int32_t StartNearlinkGatewayWithMode(const std::string &peerAddress, int32_t mode);
    int32_t StartNearlinkTerminalWithMode(const std::string &peerAddress, int32_t mode);


    static bool CanSend(uint16_t lcid, uint8_t tcid, uint8_t pi, uint64_t generation);


private:
    NearlinkIpShareService() = default;
    static void OnPeerSupported(const uint8_t peer[6], bool supported, int32_t error, uint8_t peerModes, bool known, uint64_t generation);
    static void OnPeerCapabilities(const uint8_t peer[6], uint8_t peerModes, uint64_t generation);
    static void OnConfigured(const uint8_t peer[6], bool opened, int32_t error, uint8_t mode, uint64_t generation);
    void HandlePeerSupported(const uint8_t peer[6], bool supported, int32_t error, uint8_t peerModes, bool known, uint64_t generation);
    void HandlePeerCapabilities(const uint8_t peer[6], uint8_t peerModes, uint64_t generation);
    void HandleConfigured(const uint8_t peer[6], bool opened, int32_t error, uint8_t mode, uint64_t generation);
    void HandleChannelState(bool established, int32_t error);
    void HandleGatewayPeerChannel(const uint8_t peer[6], uint64_t epoch, bool established, int32_t error);
    void ReleaseGatewayPeer(const uint8_t peer[6], uint64_t epoch);
    int32_t ValidateSecurePeer(const std::string &peerAddress, uint8_t peer[6], uint8_t &addressType) const;
    int32_t BeginRole(NearlinkIpShareRole role, const std::string &peerAddress,
        const uint8_t peer[6], uint8_t addressType, int32_t mode, uint64_t &generation);
    void SetState(NearlinkIpShareState state, const std::string &errorStage = "", int32_t error = 0);
    void NotifyStatus(const NearlinkIpShareStatus &status, const sptr<INearlinkIpShareObserver> &observer) const;
    static int32_t PrepareMode(const uint8_t peer[6], uint8_t mode, uint64_t generation);
    static bool IsSecure(const uint8_t peer[6], uint64_t generation);
    static bool IsSecureAddress(const uint8_t peer[6], uint8_t addressType, uint64_t generation);
    bool IsCurrent(uint64_t generation) const;
    void StampLocked();
    void RefreshPeerLinksLocked();
    struct GatewayPeer {
        std::array<uint8_t, 6> address{};
        uint8_t addressType{0};
        uint64_t epoch{0};
        uint8_t selectedMode{0};
        bool configured{false};
        bool active{false};
        bool releasing{false};
        std::shared_ptr<NearlinkIpShareChannel> channel;
    };
    GatewayPeer *FindGatewayPeerLocked(const uint8_t peer[6]) const;
    NearlinkIpShareCapabilities capabilities_;
    std::vector<std::unique_ptr<GatewayPeer>> gatewayPeers_;
    bool gatewayAny_{false};
    int32_t maxTerminals_{0};
    uint64_t generationCounter_ {0};
    void StopNow();

    std::mutex lifecycleMutex_;
    mutable std::mutex mutex_;
    std::condition_variable probeCondition_;
    NearlinkIpShareStatus status_;
    sptr<INearlinkIpShareObserver> observer_;
    uint8_t peer_[6] {};
    uint8_t addressType_ {0};
    uint8_t supportedPeer_[6] {};
    bool initialized_ {false};
    bool probeInProgress_ {false};
    bool peerSupported_ {false};
};

}  // namespace OHOS::Nearlink
#endif  // NEARLINK_IPSHARE_SERVICE_H
