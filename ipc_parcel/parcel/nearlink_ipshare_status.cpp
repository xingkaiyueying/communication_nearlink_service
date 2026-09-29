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
#include "nearlink_ipshare_status.h"

#include <new>

namespace OHOS::Nearlink {
namespace { bool ValidStatus(const NearlinkIpShareStatus &s); }

bool NearlinkIpShareStatus::Marshalling(Parcel &parcel) const
{
    return ValidStatus(*this) && parcel.WriteInt32(static_cast<int32_t>(role)) &&
        parcel.WriteInt32(static_cast<int32_t>(state)) &&
        parcel.WriteString(peerAddress) &&
        parcel.WriteString(ifaceName) &&
        parcel.WriteString(ipv4Address) &&
        parcel.WriteBool(hasUpstream) &&
        parcel.WriteString(errorStage) &&
        parcel.WriteInt32(errorCode) && parcel.WriteString(contextId) &&
        parcel.WriteUint64(generation) && parcel.WriteUint64(sequence) &&
        parcel.WriteInt32(static_cast<int32_t>(requestedMode)) &&
        parcel.WriteInt32(static_cast<int32_t>(selectedMode)) && parcel.WriteBool(serviceReady);
}

NearlinkIpShareStatus *NearlinkIpShareStatus::Unmarshalling(Parcel &parcel)
{
    auto *status = new (std::nothrow) NearlinkIpShareStatus();
    if (status != nullptr && !status->ReadFromParcel(parcel)) {
        delete status;
        status = nullptr;
    }
    return status;
}

namespace {
bool ValidStatus(const NearlinkIpShareStatus &s)
{
    int32_t role = static_cast<int32_t>(s.role), state = static_cast<int32_t>(s.state);
    int32_t requested = static_cast<int32_t>(s.requestedMode), selected = static_cast<int32_t>(s.selectedMode);
    return role >= 0 && role <= 2 && state >= 0 && state <= 11 &&
        (requested == 0 || IsIpShareMode(requested)) && (selected == 0 || IsIpShareMode(selected)) &&
        (selected & requested) == selected && s.peerAddress.size() <= 17 && s.ifaceName.size() <= 15 &&
        s.ipv4Address.size() <= 15 && s.errorStage.size() <= 31 && s.contextId.size() <= 64;
}
bool ReadModes(Parcel &parcel, std::vector<int32_t> &modes)
{
    int32_t count = 0, mode = 0;
    if (!parcel.ReadInt32(count) || count < 0 || count > 2) return false;
    for (int32_t i = 0; i < count; ++i) {
        if (!parcel.ReadInt32(mode) || !IsIpShareMode(mode) || (!modes.empty() && modes[0] == mode)) return false;
        modes.push_back(mode);
    }
    return true;
}
bool WriteModes(Parcel &parcel, const std::vector<int32_t> &modes)
{
    if (modes.size() > 2 || (modes.size() == 2 && modes[0] == modes[1]) ||
        !parcel.WriteInt32(static_cast<int32_t>(modes.size()))) return false;
    for (int32_t mode : modes) if (!IsIpShareMode(mode) || !parcel.WriteInt32(mode)) return false;
    return true;
}
}

bool NearlinkIpShareStatus::ReadFromParcel(Parcel &parcel)
{
    if (parcel.GetDataSize() > 64 * 1024) return false;
    NearlinkIpShareStatus value;
    int32_t role = 0, state = 0, requested = 0, selected = 0;
    if (!parcel.ReadInt32(role) || !parcel.ReadInt32(state) ||
        !parcel.ReadString(value.peerAddress) || !parcel.ReadString(value.ifaceName) ||
        !parcel.ReadString(value.ipv4Address) || !parcel.ReadBool(value.hasUpstream) ||
        !parcel.ReadString(value.errorStage) || !parcel.ReadInt32(value.errorCode) ||
        !parcel.ReadString(value.contextId) || !parcel.ReadUint64(value.generation) ||
        !parcel.ReadUint64(value.sequence) || !parcel.ReadInt32(requested) || !parcel.ReadInt32(selected) ||
        !parcel.ReadBool(value.serviceReady)) return false;
    value.role = static_cast<NearlinkIpShareRole>(role);
    value.state = static_cast<NearlinkIpShareState>(state);
    value.requestedMode = static_cast<NearlinkIpShareMode>(requested);
    value.selectedMode = static_cast<NearlinkIpShareMode>(selected);
    if (!ValidStatus(value)) return false;
    *this = value;
    return true;
}

bool NearlinkIpShareCapabilities::Marshalling(Parcel &parcel) const
{
    return discoveryState >= 0 && discoveryState <= 2 &&
        (peerCapabilityKnown || peerModes.empty()) &&
        parcel.WriteBool(identifierPresent) && parcel.WriteInt32(discoveryState) &&
        WriteModes(parcel, localModes) && WriteModes(parcel, peerModes) && parcel.WriteBool(peerCapabilityKnown);
}
bool NearlinkIpShareCapabilities::ReadFromParcel(Parcel &parcel)
{
    if (parcel.GetDataSize() > 64 * 1024) return false;
    NearlinkIpShareCapabilities value;
    value.localModes.clear();
    if (!parcel.ReadBool(value.identifierPresent) || !parcel.ReadInt32(value.discoveryState) ||
        !ReadModes(parcel, value.localModes) || !ReadModes(parcel, value.peerModes) ||
        !parcel.ReadBool(value.peerCapabilityKnown) || value.discoveryState < 0 || value.discoveryState > 2 ||
        (!value.peerCapabilityKnown && !value.peerModes.empty()) ||
        (value.peerCapabilityKnown && !value.identifierPresent)) return false;
    *this = value;
    return true;
}
NearlinkIpShareCapabilities *NearlinkIpShareCapabilities::Unmarshalling(Parcel &parcel)
{
    auto *value = new (std::nothrow) NearlinkIpShareCapabilities();
    if (value != nullptr && !value->ReadFromParcel(parcel)) { delete value; value = nullptr; }
    return value;
}

}  // namespace OHOS::Nearlink
