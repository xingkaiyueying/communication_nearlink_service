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
#include "nearlink_ipshare_channel.h"

#include <cstring>
#include <algorithm>

#include "log.h"
#include "iposl_profile.h"
#include "nearlink_ipshare_service.h"
#include "sdf_buff.h"

namespace OHOS::Nearlink {
namespace {
static_assert(DTAP_PI_IPV4 == 1, "IPoSL Demo requires IPv4 PI=0x01");
constexpr uint8_t IPV4_VERSION = 4;
constexpr uint8_t IPV4_MIN_IHL = 5;
constexpr uint8_t IPV4_PROTOCOL_UDP = 17;
constexpr uint16_t DHCP_SERVER_PORT = 67;
constexpr uint16_t DHCP_CLIENT_PORT = 68;
constexpr uint16_t UDP_HEADER_LENGTH = 8;
constexpr uint16_t DHCP_OPTIONS_OFFSET = 240;
constexpr uint16_t DHCP_MAGIC_COOKIE_OFFSET = 236;
constexpr uint8_t DHCP_BOOT_REPLY = 2;
constexpr uint8_t DHCP_OPTION_PAD = 0;
constexpr uint8_t DHCP_OPTION_MESSAGE_TYPE = 53;
constexpr uint8_t DHCP_OPTION_END = 255;
constexpr uint8_t DHCP_MESSAGE_ACK = 5;
constexpr uint8_t DHCP_MAGIC_COOKIE[] = {99, 130, 83, 99};

constexpr uint16_t IPV4_MIN_LENGTH = 20;
constexpr uint8_t IPV4_IHL_MASK = 0x0f;
constexpr uint8_t IPV4_WORD_LENGTH = 4;
constexpr uint8_t IPV4_ADDRESS_LENGTH = 4;
constexpr uint8_t IPV4_TOTAL_LENGTH_OFFSET = 2;
constexpr uint8_t IPV4_FRAGMENT_OFFSET = 6;
constexpr uint16_t IPV4_FRAGMENT_MASK = 0x3fff;
constexpr uint8_t IPV4_PROTOCOL_OFFSET = 9;
constexpr uint8_t IPV4_SOURCE_OFFSET = 12;
constexpr uint8_t IPV4_DESTINATION_OFFSET = 16;
constexpr uint32_t IPV4_BROADCAST = 0xffffffff;
constexpr uint16_t CHECKSUM_VALID = 0xffff;
constexpr uint8_t UDP_DESTINATION_OFFSET = 2;
constexpr uint8_t UDP_LENGTH_OFFSET = 4;
constexpr uint8_t UDP_CHECKSUM_OFFSET = 6;
constexpr uint8_t DHCP_BOOT_REQUEST = 1;
constexpr uint8_t DHCP_ETHERNET_TYPE = 1;
constexpr uint8_t DHCP_LAYER2_LENGTH = 6;
constexpr uint8_t DHCP_XID_OFFSET = 4;
constexpr uint8_t DHCP_CLIENT_IP_OFFSET = 12;
constexpr uint8_t DHCP_YOUR_IP_OFFSET = 16;
constexpr uint8_t DHCP_RELAY_IP_OFFSET = 24;
constexpr uint8_t DHCP_CLIENT_KEY_OFFSET = 28;
constexpr uint8_t DHCP_OPTION_SUBNET = 1;
constexpr uint8_t DHCP_OPTION_REQUESTED_IP = 50;
constexpr uint8_t DHCP_OPTION_LEASE = 51;
constexpr uint8_t DHCP_OPTION_SERVER_ID = 54;
constexpr uint8_t DHCP_OPTION_CLIENT_ID = 61;
constexpr uint8_t DHCP_MESSAGE_DISCOVER = 1;
constexpr uint8_t DHCP_MESSAGE_OFFER = 2;
constexpr uint8_t DHCP_MESSAGE_REQUEST = 3;
constexpr uint8_t DHCP_MESSAGE_DECLINE = 4;
constexpr uint8_t DHCP_MESSAGE_NAK = 6;
constexpr uint8_t DHCP_MESSAGE_RELEASE = 7;
constexpr auto DHCP_TRANSACTION_TIMEOUT = std::chrono::seconds(120);

uint32_t ReadUint32(const uint8_t *data)
{
    return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) | (uint32_t(data[2]) << 8) | data[3];
}

uint32_t SumWords(const uint8_t *data, uint16_t length, uint32_t sum)
{
    for (uint16_t i = 0; i < length; i += 2) {
        sum += uint32_t(data[i]) << 8;
        if (i + 1 < length) {
            sum += data[i + 1];
        }
    }
    while (sum >> 16) {
        sum = (sum & CHECKSUM_VALID) + (sum >> 16);
    }
    return sum;
}

bool IsUnicastAddress(uint32_t address)
{
    return (address >> 24) != 0 && (address >> 24) != 127 && (address >> 24) < 224;
}

bool IsLeaseAddress(uint32_t address, uint32_t mask)
{
    uint32_t hosts = ~mask;
    return IsUnicastAddress(address) && mask != 0 && hosts >= 3 && (hosts & (hosts + 1)) == 0 &&
           (address & hosts) != 0 && (address & hosts) != hosts;
}

uint16_t ReadUint16(const uint8_t *data)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

} // namespace

NearlinkIpShareChannel &NearlinkIpShareChannel::GetInstance()
{
    static NearlinkIpShareChannel instance;
    return instance;
}

int32_t NearlinkIpShareChannel::Initialize(const StateCallback &callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        callback_ = callback;
        HILOGI("[IpShare][Channel] initialize updated callback for active channel");
        return 0;
    }
    callback_ = callback;
    initialized_ = true;
    HILOGI("[IpShare][Channel] initialize completed: PI callbacks deferred until mode reservation");
    return 0;
}

void NearlinkIpShareChannel::Deinitialize()
{
    HILOGI("[IpShare][Channel] deinitialize started");
    Close();
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
    callback_ = nullptr;
    HILOGI("[IpShare][Channel] deinitialize completed");
}

bool NearlinkIpShareChannel::IsDrained()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return !active_ && !channelPending_ && !channelEstablished_ && !releasing_ && mode_ == 0 && !tun_.IsOpen();
}

int32_t NearlinkIpShareChannel::ResetBinding(uint64_t generation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || channelPending_ || channelEstablished_ || releasing_) {
        return -1;
    }
    enabled_ = false;
    if (mode_ != 0) {
        (void)DTAP_UnregisterProtoRecvCbk(DTAP_PI_IPV4);
    }
    if (mode_ == 3) {
        (void)DTAP_UnregisterProtoRecvCbk(DTAP_PI_IPV6);
    }
    mode_ = 0;
    generation_ = generation;
    ipv6_.Reset();
    addressSequence_ = 0;
    return 0;
}

int32_t NearlinkIpShareChannel::PrepareMode(uint8_t mode)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !initialized_ || enabled_ || channelEstablished_ || channelPending_ ||
        (mode != 0 && mode != 1 && mode != 3)) {
        return -1;
    }
    if (mode == mode_) {
        return 0;
    }
    if (mode_ != 0) {
        (void)DTAP_UnregisterProtoRecvCbk(DTAP_PI_IPV4);
    }
    if (mode_ == 3) {
        (void)DTAP_UnregisterProtoRecvCbk(DTAP_PI_IPV6);
    }
    mode_ = 0;
    if (mode == 0) {
        return 0;
    }
    if (DTAP_RegisterProtoRecvCbk(DTAP_PI_IPV4, &OnIpv4Received) != 0) {
        return -1;
    }
    if (mode == 3 && DTAP_RegisterProtoRecvCbk(DTAP_PI_IPV6, &OnIpv6Received) != 0) {
        (void)DTAP_UnregisterProtoRecvCbk(DTAP_PI_IPV4);
        return -1;
    }
    mode_ = mode;
    return 0;
}

int32_t NearlinkIpShareChannel::EnableMode(uint8_t mode)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || mode == 0 || mode != mode_) {
        return -1;
    }
    enabled_ = true;
    return 0;
}

int32_t NearlinkIpShareChannel::UpdateValidatedAddress(const NearlinkIpShareAddressEvidence &address)
{
    std::lock_guard<std::mutex> lock(mutex_);
    NearlinkIpShareIpv6::Address binary{};
    if (!active_ || !enabled_ || mode_ != 3 || !channelEstablished_ || address.generation != generation_ ||
        address.sequence <= addressSequence_ || address.prefixLength > 128 ||
        !NearlinkIpShareTun::ParseIpv6Evidence(address.address, address.ifindex, binary.data())) {
        return -1;
    }
    if (address.validLifetime && (address.flags & (0x40 | 0x08 | 0x04)) == 0 &&
        !NearlinkIpShareTun::IsIpv6AddressUsable(binary.data())) {
        return -1;
    }
    auto now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto previous = std::find_if(ipv6_.Mappings().begin(), ipv6_.Mappings().end(), [&](const auto &mapping) {
        return mapping.address == binary && mapping.terminal == !gateway_;
    });
    bool existed = previous != ipv6_.Mappings().end();
    bool wasConfirmed = existed && previous->confirmed;
    bool wasConflict = existed && previous->conflict;
    auto next = ipv6_;
    if (!next.ApplyLocal(binary, !gateway_, address.flags, address.preferredLifetime, address.validLifetime, now)) {
        return -1;
    }
    ipv6_ = std::move(next);
    addressSequence_ = address.sequence;
    auto current = std::find_if(ipv6_.Mappings().begin(), ipv6_.Mappings().end(), [&](const auto &mapping) {
        return mapping.address == binary && mapping.terminal == !gateway_;
    });
    bool present = current != ipv6_.Mappings().end();
    if (existed == present && (!present ||
        (wasConfirmed == current->confirmed && wasConflict == current->conflict))) {
        return 0;
    }
    size_t confirmed = 0;
    size_t terminalConfirmed = 0;
    size_t conflicts = 0;
    for (const auto &mapping : ipv6_.Mappings()) {
        if (mapping.confirmed) {
            ++confirmed;
            if (mapping.terminal) {
                ++terminalConfirmed;
            }
        }
        if (mapping.conflict) {
            ++conflicts;
        }
    }
    HILOGI("[IpShare][IPv6] local evidence sequence=%{public}llu flags=%{public}u "
           "preferred=%{public}u valid=%{public}u records=%{public}zu confirmed=%{public}zu "
           "terminalConfirmed=%{public}zu gatewayConfirmed=%{public}zu conflicts=%{public}zu",
           static_cast<unsigned long long>(address.sequence), address.flags,
           address.preferredLifetime, address.validLifetime, ipv6_.Mappings().size(), confirmed, terminalConfirmed,
           confirmed - terminalConfirmed, conflicts);
    return 0;
}

bool NearlinkIpShareChannel::CanSend(uint16_t lcid, uint8_t tcid, uint8_t pi, uint64_t generation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return active_ && enabled_ && channelEstablished_ && lcid == lcid_ && tcid == tcid_ && generation == generation_ &&
           (pi == 1 || (pi == 2 && mode_ == 3));
}

int NearlinkIpShareChannel::OnIpv6Received(DTAP_Data_Info_S *info, SDF_Buff_S *buffer)
{
    auto &channel = GetInstance();
    int ret = channel.Receive(info, buffer);
    if (ret != 0) {
        ++channel.rejected_;
    }
    return ret;
}

int32_t NearlinkIpShareChannel::CreateTun()
{
    HILOGI("[IpShare][Channel] create TUN requested");
    int32_t ret = tun_.Open([this](const uint8_t *data, uint16_t length) {
        if (Send(data, length) != 0) {
            ++rejected_;
            HILOGW("[IpShare][Channel] drop outbound IP packet");
        }
    });
    if (ret != 0) {
        HILOGE("[IpShare][Channel] create TUN failed ret=%{public}d", ret);
        return ret;
    }
    HILOGI("[IpShare][Channel] create TUN completed");
    return 0;
}

int32_t NearlinkIpShareChannel::SetPeer(const uint8_t peer[6], uint8_t addressType, bool gateway,
                                        const uint8_t *clientKey, const uint8_t *localLayer2, uint64_t generation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // QoSM has no creation request ID. Do not reuse the binding until old work is drained.
    if (peer == nullptr || localLayer2 == nullptr || generation == 0 || !initialized_ || channelPending_ ||
        channelEstablished_ || releasing_ || active_) {
        return -1;
    }
    (void)memcpy(peer_, peer, sizeof(peer_));
    addressType_ = addressType;
    memcpy(localLayer2_, localLayer2, sizeof(localLayer2_));
    gateway_ = gateway;
    ipv6_.Reset();
    addressSequence_ = 0;
    memcpy(clientKey_, clientKey == nullptr ? peer : clientKey, sizeof(clientKey_));
    dhcpDiscover_ = false;
    dhcpRequest_ = false;
    dhcpBound_ = false;
    boundIp_ = 0;
    active_ = true;
    generation_ = generation;
    enabled_ = false;
    rx4_ = 0;
    rx6_ = 0;
    tx4_ = 0;
    tx6_ = 0;
    rejected_ = 0;
    return 0;
}

bool NearlinkIpShareChannel::IsCurrentGeneration(uint64_t generation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return active_ && generation == generation_;
}

int32_t NearlinkIpShareChannel::Open(const uint8_t peer[6], uint8_t addressType)
{
    if (peer == nullptr) {
        HILOGE("[IpShare][Channel] open rejected: peer is null");
        return -1;
    }
    QOSM_TransChannelParams_S params = {};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !active_ || !enabled_ || releasing_ || channelPending_ || channelEstablished_ ||
            memcmp(peer_, peer, sizeof(peer_)) != 0 || addressType_ != addressType) {
            HILOGE("[IpShare][Channel] open rejected initialized=%{public}d pending=%{public}d established=%{public}d",
                   initialized_, channelPending_, channelEstablished_);
            return -1;
        }
        (void)memcpy(peer_, peer, sizeof(peer_));
        addressType_ = addressType;
        (void)memcpy(params.addr.addr, peer_, sizeof(peer_));
        params.addr.type = addressType_;
        params.linkMode = SLE_MODE_ACB;
        params.accessTransMode = ACCESS_TRANS_MODE_UNICAST;
        params.srcPort = IP_SHARE_PORT;
        params.dstPort = IP_SHARE_PORT;
        params.slqi = QOSM_TRANS_CHANNEL_SLQI_LOW;
        params.frameType = QOSM_SLE_RADIO_FRAME_TYPE_1;
        params.tcConf.mode = TRANSPORT_MODE_BASIC;
        channelPending_ = true;
    }
    HILOGI("[IpShare][Channel] QoSM create submitted port=%{public}u addressType=%{public}u", IP_SHARE_PORT,
           addressType);
    int32_t ret = QOSM_TransChannelCreate(&params);
    if (ret != 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        channelPending_ = false;
        HILOGE("[IpShare][Channel] QoSM create failed ret=%{public}d", ret);
        return -1;
    }
    HILOGI("[IpShare][Channel] QoSM create accepted; awaiting channel status");
    return 0;
}

void NearlinkIpShareChannel::Close()
{
    QOSM_TransChannelReleaseParams_S release = {};
    bool destroy = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_) {
            active_ = false;
            enabled_ = false;
        }
        if (channelEstablished_ || releasing_) {
            (void)memcpy(release.addr.addr, peer_, sizeof(peer_));
            release.addr.type = addressType_;
            release.tcid = tcid_;
            destroy = true;
            releasing_ = true;
        }
        if (mode_ != 0) {
            (void)DTAP_UnregisterProtoRecvCbk(DTAP_PI_IPV4);
        }
        if (mode_ == 3) {
            (void)DTAP_UnregisterProtoRecvCbk(DTAP_PI_IPV6);
        }
        mode_ = 0;
        // Retain pending creation and release identity for late completion and repeated Stop.
        channelEstablished_ = false;
        dhcpBound_ = false;
        dhcpRequest_ = false;
        boundIp_ = 0;
        ipv6_.Reset();
    }
    if (destroy) {
        int32_t ret = QOSM_TransChannelDestroy(&release);
        if (ret != 0) {
            HILOGE("[IpShare][Channel] QoSM destroy failed tcid=%{public}u ret=%{public}d", release.tcid, ret);
        } else {
            HILOGI("[IpShare][Channel] QoSM destroy completed tcid=%{public}u", release.tcid);
        }
    }
    tun_.Close();
    HILOGI("[IpShare][Channel] closed rx4=%{public}llu rx6=%{public}llu tx4=%{public}llu tx6=%{public}llu "
           "rejected=%{public}llu",
           static_cast<unsigned long long>(rx4_.load()), static_cast<unsigned long long>(rx6_.load()),
           static_cast<unsigned long long>(tx4_.load()), static_cast<unsigned long long>(tx6_.load()),
           static_cast<unsigned long long>(rejected_.load()));
}

bool NearlinkIpShareChannel::IsIpSharePort(uint16_t port)
{
    return port == IP_SHARE_PORT;
}

bool NearlinkIpShareChannel::IsAcceptingPort(uint16_t port)
{
    return GetInstance().CanAccept(port);
}

bool NearlinkIpShareChannel::CanAccept(uint16_t port)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const uint8_t emptyPeer[6] = {0};
    bool accept = IsIpSharePort(port) && initialized_ && enabled_ && active_ && !releasing_ && !channelPending_ &&
                  !channelEstablished_ && tun_.IsOpen() && memcmp(peer_, emptyPeer, sizeof(peer_)) != 0;
    if (accept) {
        channelPending_ = true;
    } // Passive creation has the same late-completion race as Open.
    return accept;
}

bool NearlinkIpShareChannel::HandleChannelStatus(const QOSM_TransChannelRspParams_S *params)
{
    return GetInstance().ConsumeStatus(params);
}

bool NearlinkIpShareChannel::ConsumeStatus(const QOSM_TransChannelRspParams_S *params)
{
    if (params == nullptr || (!IsIpSharePort(params->srcPort) && !IsIpSharePort(params->dstPort))) {
        return false;
    }
    StateCallback callback;
    bool established = false;
    bool destroy = false;
    int32_t error = 0;
    uint64_t generation = 0;
    QOSM_TransChannelReleaseParams_S release = {};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (memcmp(params->addr.addr, peer_, sizeof(peer_)) != 0 || params->addr.type != addressType_) {
            return false;
        }
        if (params->status == QOSM_TRANS_CHANNEL_ESTABLISHED) {
            if (channelEstablished_ && (lcid_ != params->lcid || tcid_ != params->tcid)) {
                // An unrelated success must never replace the current channel.
                release.addr = params->addr;
                release.tcid = params->tcid;
                destroy = true;
            } else {
                channelPending_ = false;
                lcid_ = params->lcid;
                tcid_ = params->tcid;
                if (!initialized_ || !active_ || releasing_) {
                    releasing_ = true;
                    release.addr = params->addr;
                    release.tcid = tcid_;
                    destroy = true;
                } else {
                    channelEstablished_ = true;
                    established = true;
                    callback = callback_;
                }
            }
        } else if (params->status == QOSM_TRANS_CHANNEL_ESTABLISH_FAIL) {
            if (!channelPending_) {
                return true;
            }
            channelPending_ = false;
            if (active_) {
                callback = callback_;
                error = -1;
            }
        } else if (params->status == QOSM_TRANS_CHANNEL_RELEASED || params->status == QOSM_TRANS_CHANNEL_RELEASE_FAIL) {
            if ((!channelEstablished_ && !releasing_) || params->lcid != lcid_ || params->tcid != tcid_) {
                return true;
            }
            // Keep release failures retryable; QoSM removes its record only on RELEASED.
            releasing_ = params->status == QOSM_TRANS_CHANNEL_RELEASE_FAIL;
            channelEstablished_ = false;
            dhcpBound_ = false;
            dhcpRequest_ = false;
            boundIp_ = 0;
            if (active_) {
                callback = callback_;
                error = params->status == QOSM_TRANS_CHANNEL_RELEASED ? 0 : -1;
            }
        }
        if (!active_ && !channelPending_ && !releasing_ && !channelEstablished_) {
            callback = callback_;
        }
        generation = generation_;
    }
    if (destroy) {
        int32_t ret = QOSM_TransChannelDestroy(&release);
        HILOGI("[IpShare][Channel] late/cancelled channel cleanup tcid=%{public}u ret=%{public}d", release.tcid, ret);
    }
    if (callback) {
        callback(established, error, generation);
    }
    return true;
}

int NearlinkIpShareChannel::OnIpv4Received(DTAP_Data_Info_S *info, SDF_Buff_S *buffer)
{
    auto &channel = GetInstance();
    int ret = channel.Receive(info, buffer);
    if (ret != 0) {
        ++channel.rejected_;
    }
    return ret;
}

int NearlinkIpShareChannel::Receive(DTAP_Data_Info_S *info, SDF_Buff_S *buffer)
{
    if (info == nullptr || buffer == nullptr || (info->pi != DTAP_PI_IPV4 && info->pi != DTAP_PI_IPV6)) {
        HILOGE("[IpShare][RX] packet rejected: invalid DTAP input");
        return -1;
    }
    const uint8_t *data = SDF_DataOffset(buffer);
    uint32_t dataLen = SDF_DataLenGet(buffer);
    bool bound = false;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_ || !channelEstablished_ || info->lcid != lcid_ || info->tcid != tcid_ || dataLen > UINT16_MAX) {
            HILOGW("[IpShare][RX] packet rejected: channel mismatch lcid=%{public}u tcid=%{public}u "
                   "length=%{public}u",
                   info->lcid, info->tcid, dataLen);
            return -1;
        }
        bound = DhcpBoundLocked();
        generation = generation_;
    }
    uint16_t length = static_cast<uint16_t>(dataLen);
    if (!IposlCodecValidatePacket(info->pi, data, length) ||
        !NearlinkIpShareService::CanSend(info->lcid, info->tcid, info->pi, generation)) {
        return -1;
    }
    if (!AuthorizePacket(data, length, generation, true)) {
        HILOGW("[IpShare][RX] packet rejected by IP policy pi=%{public}u length=%{public}u dhcpBound=%{public}d",
               info->pi, length, bound);
        return -1;
    }
    /* Recheck under the ownership lock through delivery; stop cannot close/reopen underneath it. */
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !enabled_ || generation != generation_) {
        return -1;
    }
    int32_t ret = tun_.Write(data, length);
    if (ret != 0) {
        HILOGE("[IpShare][RX] delivery to TUN failed ret=%{public}d length=%{public}u", ret, length);
        return ret;
    }
    if (info->pi == 1) {
        ++rx4_;
    } else {
        ++rx6_;
    }
    HILOGD("[IpShare][RX] pi=%{public}u lcid=%{public}u tcid=%{public}u sdu=%{public}u", info->pi, info->lcid,
           info->tcid, length);
    return 0;
}

int32_t NearlinkIpShareChannel::Send(const uint8_t *data, uint16_t length)
{
    std::vector<uint8_t> adapted;
    if (data && length >= 48 && data[0] >> 4 == 6 && data[6] == 58 && data[40] >= 133 && data[40] <= 136) {
        adapted.assign(data, data + length);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!NearlinkIpShareIpv6::AddLayer2Option(adapted, localLayer2_)) {
                return -1;
            }
        }
        data = adapted.data();
        length = static_cast<uint16_t>(adapted.size());
    }
    uint16_t lcid = 0;
    uint8_t tcid = 0;
    bool bound = false;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_ || !channelEstablished_) {
            HILOGW("[IpShare][TX] packet rejected: QoSM channel not established");
            return -1;
        }
        lcid = lcid_;
        tcid = tcid_;
        bound = DhcpBoundLocked();
        generation = generation_;
    }
    if (!AuthorizePacket(data, length, generation, false)) {
        uint8_t version = data != nullptr && length != 0 ? data[0] >> 4 : 0;
        HILOGW("[IpShare][TX] packet rejected by IP policy version=%{public}u length=%{public}u dhcpBound=%{public}d",
               version, length, bound);
        return -1;
    }
    uint8_t pi = data[0] >> 4 == 6 ? DTAP_PI_IPV6 : DTAP_PI_IPV4;
    int32_t ret = IposlProfileSendIp(lcid, tcid, pi, data, length, generation);
    if (ret != 0) {
        HILOGE("[IpShare][TX] DTAP send failed lcid=%{public}u tcid=%{public}u ret=%{public}d", lcid, tcid, ret);
        return -1;
    }
    if (pi == 1) {
        ++tx4_;
    } else {
        ++tx6_;
    }
    HILOGD("[IpShare][TX] pi=%{public}u lcid=%{public}u tcid=%{public}u sdu=%{public}u", pi, lcid, tcid, length);
    return 0;
}

bool NearlinkIpShareChannel::DhcpBoundLocked()
{
    if (dhcpBound_ && std::chrono::steady_clock::now() >= leaseExpiry_) {
        dhcpBound_ = false;
        boundIp_ = 0;
    }
    return dhcpBound_;
}

bool NearlinkIpShareChannel::ParseDhcpOptions(const uint8_t *data, uint16_t length, DhcpPacket &packet)
{
    bool haveRequested = false;
    bool haveServer = false;
    bool haveLease = false;
    bool haveClient = false;
    for (uint16_t offset = DHCP_OPTIONS_OFFSET; offset < length;) {
        uint8_t option = data[offset++];
        if (option == DHCP_OPTION_PAD) {
            continue;
        }
        if (option == DHCP_OPTION_END) {
            return packet.message != 0;
        }
        if (offset == length) {
            return false;
        }
        uint8_t count = data[offset++];
        if (count > length - offset) {
            return false;
        }
        if (option == DHCP_OPTION_MESSAGE_TYPE) {
            if (packet.message != 0 || count != 1) {
                return false;
            }
            packet.message = data[offset];
        } else if (option == DHCP_OPTION_CLIENT_ID) {
            if (haveClient || count != DHCP_LAYER2_LENGTH + 1 || data[offset] != DHCP_ETHERNET_TYPE ||
                memcmp(data + offset + 1, packet.key, DHCP_LAYER2_LENGTH) != 0) {
                return false;
            }
            haveClient = true;
        } else if (option == DHCP_OPTION_REQUESTED_IP || option == DHCP_OPTION_SERVER_ID ||
                   option == DHCP_OPTION_LEASE || option == DHCP_OPTION_SUBNET) {
            bool *present = &haveRequested;
            uint32_t *value = &packet.requested;
            if (option == DHCP_OPTION_SERVER_ID) {
                present = &haveServer;
                value = &packet.server;
            } else if (option == DHCP_OPTION_LEASE) {
                present = &haveLease;
                value = &packet.lease;
            } else if (option == DHCP_OPTION_SUBNET) {
                present = &packet.haveSubnet;
                value = &packet.subnet;
            }
            if (*present || count != IPV4_ADDRESS_LENGTH) {
                return false;
            }
            *present = true;
            *value = ReadUint32(data + offset);
        }
        offset += count;
    }
    return false;
}

bool NearlinkIpShareChannel::ParseDhcpPacket(const uint8_t *data, uint16_t length, DhcpPacket &packet)
{
    if (!ValidateIpv4(data, length) || !IsDhcpPacket(data, length)) {
        return false;
    }
    uint16_t header = (data[0] & IPV4_IHL_MASK) * IPV4_WORD_LENGTH;
    uint16_t udpLength = ReadUint16(data + header + UDP_LENGTH_OFFSET);
    if (udpLength < UDP_HEADER_LENGTH + DHCP_OPTIONS_OFFSET || header + udpLength != length ||
        SumWords(data, header, 0) != CHECKSUM_VALID) {
        return false;
    }
    uint32_t pseudoSum = SumWords(data + IPV4_SOURCE_OFFSET, 2 * IPV4_ADDRESS_LENGTH, IPV4_PROTOCOL_UDP + udpLength);
    if (ReadUint16(data + header + UDP_CHECKSUM_OFFSET) != 0 &&
        SumWords(data + header, udpLength, pseudoSum) != CHECKSUM_VALID) {
        return false;
    }
    const uint8_t *dhcp = data + header + UDP_HEADER_LENGTH;
    packet.request = ReadUint16(data + header) == DHCP_CLIENT_PORT;
    if (dhcp[0] != (packet.request ? DHCP_BOOT_REQUEST : DHCP_BOOT_REPLY) || dhcp[1] != DHCP_ETHERNET_TYPE ||
        dhcp[2] != DHCP_LAYER2_LENGTH || dhcp[3] != 0 ||
        memcmp(dhcp + DHCP_MAGIC_COOKIE_OFFSET, DHCP_MAGIC_COOKIE, sizeof(DHCP_MAGIC_COOKIE)) != 0) {
        return false;
    }
    packet.source = ReadUint32(data + IPV4_SOURCE_OFFSET);
    packet.client = ReadUint32(dhcp + DHCP_CLIENT_IP_OFFSET);
    packet.offered = ReadUint32(dhcp + DHCP_YOUR_IP_OFFSET);
    packet.requested = packet.client;
    packet.xid = ReadUint32(dhcp + DHCP_XID_OFFSET);
    memcpy(packet.key, dhcp + DHCP_CLIENT_KEY_OFFSET, sizeof(packet.key));
    if (!ParseDhcpOptions(dhcp, udpLength - UDP_HEADER_LENGTH, packet)) {
        return false;
    }
    // This platform server uses its own address in giaddr for a direct L3 reply.
    uint32_t relay = ReadUint32(dhcp + DHCP_RELAY_IP_OFFSET);
    if (packet.request) {
        return relay == 0 &&
               (packet.source == 0 || (packet.source == packet.client && IsUnicastAddress(packet.source)));
    }
    uint32_t destination = ReadUint32(data + IPV4_DESTINATION_OFFSET);
    return (relay == 0 || relay == packet.server) && packet.source == packet.server &&
           IsUnicastAddress(packet.server) &&
           (destination == IPV4_BROADCAST || destination == packet.offered || destination == packet.client);
}

bool NearlinkIpShareChannel::HandleDhcpRequestLocked(const DhcpPacket &packet)
{
    if (packet.source != 0 && (!DhcpBoundLocked() || packet.source != boundIp_)) {
        return false;
    }
    if (packet.message == DHCP_MESSAGE_DISCOVER) {
        dhcpBound_ = false;
        boundIp_ = 0;
        dhcpRequest_ = false;
        dhcpDiscover_ = true;
    } else if (packet.message == DHCP_MESSAGE_REQUEST && IsUnicastAddress(packet.requested)) {
        // A client need not know its subnet until the server's ACK. Never infer /24 from the address.
        if (packet.haveSubnet && !IsLeaseAddress(packet.requested, packet.subnet)) {
            return false;
        }
        requestedIp_ = packet.requested;
        serverIp_ = packet.server;
        dhcpRequest_ = true;
    } else if (packet.message == DHCP_MESSAGE_DECLINE || packet.message == DHCP_MESSAGE_RELEASE) {
        if (!DhcpBoundLocked() || packet.requested != boundIp_) {
            return false;
        }
        dhcpBound_ = false;
        boundIp_ = 0;
        dhcpRequest_ = false;
        dhcpDiscover_ = false;
        return true;
    } else {
        return false;
    }
    dhcpXid_ = packet.xid;
    memcpy(dhcpKey_, packet.key, sizeof(dhcpKey_));
    transactionExpiry_ = std::chrono::steady_clock::now() + DHCP_TRANSACTION_TIMEOUT;
    return true;
}

bool NearlinkIpShareChannel::HandleDhcpReplyLocked(const DhcpPacket &packet)
{
    auto now = std::chrono::steady_clock::now();
    if (now >= transactionExpiry_ || packet.xid != dhcpXid_ || memcmp(dhcpKey_, packet.key, sizeof(dhcpKey_)) != 0) {
        return false;
    }
    if (packet.message == DHCP_MESSAGE_OFFER) {
        return dhcpDiscover_ && IsUnicastAddress(packet.offered) &&
               (!packet.haveSubnet || IsLeaseAddress(packet.offered, packet.subnet));
    }
    if (!dhcpRequest_ || (serverIp_ != 0 && serverIp_ != packet.server)) {
        return false;
    }
    if (packet.message == DHCP_MESSAGE_NAK) {
        dhcpBound_ = false;
        boundIp_ = 0;
        dhcpRequest_ = false;
        dhcpDiscover_ = false;
        return true;
    }
    if (packet.message != DHCP_MESSAGE_ACK || packet.offered != requestedIp_ || packet.lease == 0 ||
        !packet.haveSubnet || !IsLeaseAddress(packet.offered, packet.subnet)) {
        return false;
    }
    boundIp_ = requestedIp_;
    leaseExpiry_ = now + std::chrono::seconds(packet.lease);
    dhcpBound_ = true;
    dhcpRequest_ = false;
    dhcpDiscover_ = false;
    HILOGI("[IpShare] authorized REQUEST/ACK; IPv4 binding established");
    return true;
}

bool NearlinkIpShareChannel::ObserveDhcp(const uint8_t *data, uint16_t length, uint64_t generation)
{
    DhcpPacket packet{};
    if (!ParseDhcpPacket(data, length, packet)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !enabled_ || !channelEstablished_ || generation != generation_ ||
        memcmp(clientKey_, packet.key, sizeof(clientKey_)) != 0) {
        return false;
    }
    return packet.request ? HandleDhcpRequestLocked(packet) : HandleDhcpReplyLocked(packet);
}

bool NearlinkIpShareChannel::AuthorizePacket(const uint8_t *data, uint16_t length, uint64_t generation, bool received)
{
    uint8_t pi = data != nullptr && length != 0 && data[0] >> 4 == 6 ? 2 : 1;
    if (!IposlCodecValidatePacket(pi, data, length)) {
        return false;
    }
    if (pi == 2) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_ || !enabled_ || !channelEstablished_ || generation != generation_ || mode_ != 3) {
            return false;
        }
        const uint8_t unspecified[16]{};
        auto seconds =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        bool localControl = data[6] == 58 && length >= 48 && data[40] >= 133 && data[40] <= 136;
        bool verifyLocal = !received && memcmp(data + 8, unspecified, 16) != 0 && (!gateway_ || localControl);
        auto next = ipv6_;
        NearlinkIpShareIpv6::Address local{};
        std::copy(data + 8, data + 24, local.begin());
        auto now = static_cast<uint64_t>(seconds);
        // Revalidate at most once per monotonic second; generation reset and address
        // evidence still revoke cached records immediately.
        if (verifyLocal && !next.LocalUsable(local, !gateway_, now) &&
            (!NearlinkIpShareTun::IsIpv6AddressUsable(data + 8) || !next.ObserveKernelLocal(local, !gateway_, now))) {
            return false;
        }
        size_t oldRecords = ipv6_.Mappings().size();
        size_t oldConfirmed = std::count_if(ipv6_.Mappings().begin(), ipv6_.Mappings().end(),
                                            [](const auto &mapping) { return mapping.confirmed; });
        size_t oldConflicts = std::count_if(ipv6_.Mappings().begin(), ipv6_.Mappings().end(),
                                            [](const auto &mapping) { return mapping.conflict; });
        bool allowed = next.Authorize(data, length, gateway_ == received, received ? peer_ : localLayer2_,
                                      static_cast<uint64_t>(seconds));
        if (allowed) {
            ipv6_ = std::move(next);
            size_t confirmed = std::count_if(ipv6_.Mappings().begin(), ipv6_.Mappings().end(),
                                             [](const auto &mapping) { return mapping.confirmed; });
            size_t terminalConfirmed =
                std::count_if(ipv6_.Mappings().begin(), ipv6_.Mappings().end(),
                              [](const auto &mapping) { return mapping.confirmed && mapping.terminal; });
            size_t conflicts = std::count_if(ipv6_.Mappings().begin(), ipv6_.Mappings().end(),
                                             [](const auto &mapping) { return mapping.conflict; });
            if (oldRecords != ipv6_.Mappings().size() || oldConfirmed != confirmed || oldConflicts != conflicts) {
                HILOGI("[IpShare][IPv6] mapping transition generation=%{public}llu direction=%{public}s "
                       "records=%{public}zu confirmed=%{public}zu terminalConfirmed=%{public}zu "
                       "gatewayConfirmed=%{public}zu conflicts=%{public}zu",
                       static_cast<unsigned long long>(generation_), received ? "rx" : "tx", ipv6_.Mappings().size(),
                       confirmed, terminalConfirmed, confirmed - terminalConfirmed, conflicts);
            }
        }
        return allowed;
    }
    bool fromClient;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_ || !enabled_ || !channelEstablished_ || generation != generation_) {
            return false;
        }
        fromClient = gateway_ == received;
    }
    if (IsDhcpPacket(data, length)) {
        uint16_t header = (data[0] & IPV4_IHL_MASK) * IPV4_WORD_LENGTH;
        if ((ReadUint16(data + header) == DHCP_CLIENT_PORT) != fromClient) {
            return false;
        }
        return ObserveDhcp(data, length, generation);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !enabled_ || !channelEstablished_ || generation != generation_ || !DhcpBoundLocked()) {
        return false;
    }
    // The negotiated Demo mode is unicast. Public Internet reply sources remain valid.
    return ReadUint32(data + (fromClient ? IPV4_SOURCE_OFFSET : IPV4_DESTINATION_OFFSET)) == boundIp_;
}

bool NearlinkIpShareChannel::ValidateIpv4(const uint8_t *data, uint16_t length)
{
    if (data == nullptr || length < IPV4_MIN_LENGTH || (data[0] >> 4) != IPV4_VERSION ||
        (data[0] & IPV4_IHL_MASK) < IPV4_MIN_IHL) {
        return false;
    }
    uint16_t header = (data[0] & IPV4_IHL_MASK) * IPV4_WORD_LENGTH;
    return header <= length && ReadUint16(data + IPV4_TOTAL_LENGTH_OFFSET) == length && length <= IPOSL_MTU;
}

bool NearlinkIpShareChannel::IsDhcpPacket(const uint8_t *data, uint16_t length)
{
    // Called only after the basic IPv4 bounds have been checked.
    uint16_t header = (data[0] & IPV4_IHL_MASK) * IPV4_WORD_LENGTH;
    if ((ReadUint16(data + IPV4_FRAGMENT_OFFSET) & IPV4_FRAGMENT_MASK) != 0 ||
        data[IPV4_PROTOCOL_OFFSET] != IPV4_PROTOCOL_UDP || length < header + UDP_HEADER_LENGTH) {
        return false;
    }
    uint16_t source = ReadUint16(data + header);
    uint16_t destination = ReadUint16(data + header + UDP_DESTINATION_OFFSET);
    return (source == DHCP_SERVER_PORT && destination == DHCP_CLIENT_PORT) ||
           (source == DHCP_CLIENT_PORT && destination == DHCP_SERVER_PORT);
}

} // namespace OHOS::Nearlink
