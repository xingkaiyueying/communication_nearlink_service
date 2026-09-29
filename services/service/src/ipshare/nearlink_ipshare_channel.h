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
#ifndef NEARLINK_IPSHARE_CHANNEL_H
#define NEARLINK_IPSHARE_CHANNEL_H

#include <chrono>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

#include "dtap.h"
#include "nearlink_ipshare_tun.h"
#include "nearlink_ipshare_ipv6.h"
#include "nearlink_ipshare_status.h"
#include "qosm_trans_channel.h"

namespace OHOS::Nearlink {

class NearlinkIpShareChannel final {
public:
    static constexpr uint16_t IP_SHARE_PORT = 30200;
    using StateCallback = std::function<void(bool, int32_t, uint64_t)>;

    static NearlinkIpShareChannel &GetInstance();

    int32_t Initialize(const StateCallback &callback);
    void Deinitialize();
    int32_t CreateTun();
    int32_t Open(const uint8_t peer[6], uint8_t addressType);
    void Close();
    int32_t SetPeer(const uint8_t peer[6], uint8_t addressType, bool gateway = true,
                    const uint8_t *clientKey = nullptr, const uint8_t *localLayer2 = nullptr, uint64_t generation = 0);
    int32_t PrepareMode(uint8_t mode);
    bool IsDrained();
    int32_t UpdateValidatedAddress(const NearlinkIpShareAddressEvidence &address);
    int32_t ResetBinding(uint64_t generation);
    int32_t EnableMode(uint8_t mode);
    bool CanSend(uint16_t lcid, uint8_t tcid, uint8_t pi, uint64_t generation);
    static int OnIpv6Received(DTAP_Data_Info_S *info, SDF_Buff_S *buffer);
    bool IsCurrentGeneration(uint64_t generation);

    static bool IsIpSharePort(uint16_t port);
    static bool IsAcceptingPort(uint16_t port);
    static bool HandleChannelStatus(const QOSM_TransChannelRspParams_S *params);
    static int OnIpv4Received(DTAP_Data_Info_S *info, SDF_Buff_S *buffer);

private:
    NearlinkIpShareChannel() = default;
    bool ConsumeStatus(const QOSM_TransChannelRspParams_S *params);
    bool CanAccept(uint16_t port);
    int Receive(DTAP_Data_Info_S *info, SDF_Buff_S *buffer);
    int32_t Send(const uint8_t *data, uint16_t length);
    struct DhcpPacket {
        bool request{false};
        bool haveSubnet{false};
        uint8_t message{0};
        uint8_t key[6]{};
        uint32_t source{0};
        uint32_t client{0};
        uint32_t offered{0};
        uint32_t requested{0};
        uint32_t server{0};
        uint32_t lease{0};
        uint32_t subnet{0};
        uint32_t xid{0};
    };
    static bool ValidateIpv4(const uint8_t *data, uint16_t length);
    static bool IsDhcpPacket(const uint8_t *data, uint16_t length);
    static bool ParseDhcpOptions(const uint8_t *data, uint16_t length, DhcpPacket &packet);
    static bool ParseDhcpPacket(const uint8_t *data, uint16_t length, DhcpPacket &packet);
    bool HandleDhcpRequestLocked(const DhcpPacket &packet);
    bool HandleDhcpReplyLocked(const DhcpPacket &packet);
    bool ObserveDhcp(const uint8_t *data, uint16_t length, uint64_t generation);
    bool AuthorizePacket(const uint8_t *data, uint16_t length, uint64_t generation, bool received);
    bool gateway_{true};
    NearlinkIpShareIpv6 ipv6_;
    uint64_t addressSequence_{0};
    uint8_t clientKey_[6]{};
    bool dhcpDiscover_{false};
    std::chrono::steady_clock::time_point transactionExpiry_{};
    bool DhcpBoundLocked();
    uint8_t dhcpKey_[6]{};
    uint32_t dhcpXid_{0};
    uint32_t requestedIp_{0};
    uint32_t serverIp_{0};
    uint32_t boundIp_{0};
    bool dhcpRequest_{false};
    std::chrono::steady_clock::time_point leaseExpiry_{};

    std::mutex mutex_;
    NearlinkIpShareTun tun_;
    StateCallback callback_;
    uint8_t peer_[6]{};
    uint8_t localLayer2_[6]{};
    uint8_t mode_{0};
    bool enabled_{false};
    std::atomic<uint64_t> rx4_{0}, rx6_{0}, tx4_{0}, tx6_{0}, rejected_{0};
    uint8_t addressType_{0};
    uint16_t lcid_{0};
    uint8_t tcid_{0};
    uint64_t generation_{0};
    bool active_{false};
    bool releasing_{false};
    bool initialized_{false};
    bool channelPending_{false};
    bool channelEstablished_{false};
    bool dhcpBound_{false};
};

} // namespace OHOS::Nearlink
#endif // NEARLINK_IPSHARE_CHANNEL_H
