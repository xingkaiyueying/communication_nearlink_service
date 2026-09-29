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
#ifndef NEARLINK_IPSHARE_STATUS_H
#define NEARLINK_IPSHARE_STATUS_H

#include <cstdint>
#include <string>
#include <vector>

#include "parcel.h"

namespace OHOS::Nearlink {

struct NearlinkIpShareAddressEvidence {
    uint64_t generation{0}, sequence{0};
    std::string address;
    uint32_t ifindex{0}, prefixLength{0}, flags{0}, preferredLifetime{0}, validLifetime{0};
    bool Write(Parcel &p) const
    {
        return generation && sequence && !address.empty() && address.size() <= 45 && ifindex &&
            prefixLength <= 128 && preferredLifetime <= validLifetime &&
            p.WriteUint64(generation) && p.WriteUint64(sequence) && p.WriteString(address) &&
            p.WriteUint32(ifindex) && p.WriteUint32(prefixLength) && p.WriteUint32(flags) &&
            p.WriteUint32(preferredLifetime) && p.WriteUint32(validLifetime);
    }
    bool Read(Parcel &p)
    {
        NearlinkIpShareAddressEvidence a;
        if (!p.ReadUint64(a.generation) || !p.ReadUint64(a.sequence) || !p.ReadString(a.address) ||
            !p.ReadUint32(a.ifindex) || !p.ReadUint32(a.prefixLength) || !p.ReadUint32(a.flags) ||
            !p.ReadUint32(a.preferredLifetime) || !p.ReadUint32(a.validLifetime) ||
            !a.generation || !a.sequence || a.address.empty() || a.address.size()>45 || !a.ifindex ||
            a.prefixLength>128 || a.preferredLifetime>a.validLifetime) return false;
        *this = a; return true;
    }
};

enum class NearlinkIpShareMode : int32_t { NONE = 0, IPV4 = 1, DUAL_STACK = 3 };
inline bool IsIpShareMode(int32_t mode) { return mode == 1 || mode == 3; }

class NearlinkIpShareCapabilities final : public Parcelable {
public:
    bool identifierPresent {false};
    int32_t discoveryState {0}; // 0 unknown, 1 discovered, 2 absent
    std::vector<int32_t> localModes {1, 3};
    std::vector<int32_t> peerModes;
    bool peerCapabilityKnown {false};
    bool Marshalling(Parcel &parcel) const override;
    static NearlinkIpShareCapabilities *Unmarshalling(Parcel &parcel);
    bool ReadFromParcel(Parcel &parcel);
};


enum class NearlinkIpShareRole : int32_t {
    NONE = 0,
    GATEWAY = 1,
    TERMINAL = 2,
};

enum class NearlinkIpShareState : int32_t {
    IDLE = 0,
    STARTING = 1,
    DISCOVERING = 2,
    CONFIGURING = 3,
    IFACE_READY = 4,
    CHANNEL_READY = 5,
    DHCP = 6,
    SERVING = 7,
    SERVING_NO_UPSTREAM = 8,
    ACTIVE = 9,
    STOPPING = 10,
    ERROR = 11,
};

class NearlinkIpShareStatus final : public Parcelable {
public:
    NearlinkIpShareRole role {NearlinkIpShareRole::NONE};
    NearlinkIpShareState state {NearlinkIpShareState::IDLE};
    std::string peerAddress;
    std::string ifaceName;
    std::string ipv4Address;
    bool hasUpstream {false};
    std::string errorStage;
    int32_t errorCode {0};
    std::string contextId;
    uint64_t generation {0};
    uint64_t sequence {0};
    NearlinkIpShareMode requestedMode {NearlinkIpShareMode::NONE};
    NearlinkIpShareMode selectedMode {NearlinkIpShareMode::NONE};
    bool serviceReady {false};

    bool Marshalling(Parcel &parcel) const override;
    static NearlinkIpShareStatus *Unmarshalling(Parcel &parcel);
    bool ReadFromParcel(Parcel &parcel);
};

}  // namespace OHOS::Nearlink
#endif  // NEARLINK_IPSHARE_STATUS_H
