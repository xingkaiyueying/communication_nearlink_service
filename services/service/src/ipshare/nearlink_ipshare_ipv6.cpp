/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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
#include "nearlink_ipshare_ipv6.h"

#include <algorithm>
#include <cstring>

namespace OHOS::Nearlink {
namespace {
constexpr uint32_t ADDRESS_DAD_FAILED = 0x08;
constexpr uint32_t ADDRESS_UNUSABLE_FLAGS = 0x40 | ADDRESS_DAD_FAILED | 0x04;
constexpr uint64_t KERNEL_CACHE_SECONDS = 1;
} // namespace
void NearlinkIpShareIpv6::Reset()
{
    *this = NearlinkIpShareIpv6{};
}

bool NearlinkIpShareIpv6::ApplyLocal(const Address &address, bool terminal, uint32_t flags, uint32_t preferred,
                                     uint32_t valid, uint64_t now)
{
    Expire(now);
    if (preferred > valid || !Unicast(address)) {
        return false;
    }
    if (valid == 0) {
        mappings_.erase(
            std::remove_if(mappings_.begin(), mappings_.end(),
                           [&](const Mapping &m) { return m.address == address && m.terminal == terminal; }),
            mappings_.end());
        return true;
    }
    if (!Candidate(address, terminal, now)) {
        return false;
    }
    auto record = Find(address, terminal);
    record->preferredUntil = Until(now, preferred);
    record->validUntil = Until(now, valid);
    record->conflict = (flags & ADDRESS_DAD_FAILED) != 0;
    record->confirmed = false;
    if ((flags & ADDRESS_UNUSABLE_FLAGS) != 0) {
        return true;
    }
    if (!ConfirmOwner(address, terminal, now)) {
        return false;
    }
    Find(address, terminal)->kernelUntil = now + KERNEL_CACHE_SECONDS;
    return true;
}

bool NearlinkIpShareIpv6::ObserveKernelLocal(const Address &address, bool terminal, uint64_t now)
{
    Expire(now);
    if (!ConfirmOwner(address, terminal, now)) {
        return false;
    }
    Find(address, terminal)->kernelUntil = now + KERNEL_CACHE_SECONDS;
    return true;
}

bool NearlinkIpShareIpv6::LocalUsable(const Address &address, bool terminal, uint64_t now)
{
    Expire(now);
    auto record = Find(address, terminal);
    return record && record->confirmed && !record->conflict && record->kernelUntil > now;
}

bool NearlinkIpShareIpv6::AddLayer2Option(std::vector<uint8_t> &packet, const uint8_t sender[6])
{
    if (packet.size() < 48 || packet[6] != 58 || packet[40] < 133 || packet[40] > 136) {
        return true;
    }
    uint8_t type = packet[40];
    size_t fixed = type == 133 ? 8 : (type == 134 ? 16 : 24);
    if (packet.size() < 40 + fixed || !Checksum(packet.data(), packet.size(), 40, 58)) {
        return false;
    }
    if (Zero(Addr(packet.data() + 8))) {
        return true;
    }
    for (size_t i = 40 + fixed; i < packet.size();) {
        if (packet.size() - i < 2 || packet[i + 1] == 0 || size_t(packet[i + 1]) * 8 > packet.size() - i) {
            return false;
        }
        if (packet[i] == 1 || packet[i] == 2) {
            return true; // validator checks identity and direction
        }
        i += size_t(packet[i + 1]) * 8;
    }
    if (packet.size() + 8 > 1500) {
        return false;
    }
    packet.push_back(type == 136 ? 2 : 1);
    packet.push_back(1);
    packet.insert(packet.end(), sender, sender + 6);
    size_t payload = packet.size() - 40;
    packet[4] = payload >> 8;
    packet[5] = payload;
    packet[42] = packet[43] = 0;
    uint16_t checksum = ~Sum(packet.data() + 40, payload, Sum(packet.data() + 8, 32, 58 + payload));
    packet[42] = checksum >> 8;
    packet[43] = checksum;
    return true;
}

void NearlinkIpShareIpv6::Expire(uint64_t now)
{
    mappings_.erase(std::remove_if(mappings_.begin(), mappings_.end(),
                                   [now](const Mapping &m) {
                                       return m.validUntil <= now || (!m.confirmed && m.candidateUntil <= now);
                                   }),
                    mappings_.end());
    prefixes_.erase(
        std::remove_if(prefixes_.begin(), prefixes_.end(), [now](const Prefix &p) { return p.validUntil <= now; }),
        prefixes_.end());
}

bool NearlinkIpShareIpv6::Authorize(const uint8_t *packet, size_t length, bool terminal, const uint8_t sender[6],
                                    uint64_t now)
{
    Expire(now);
    // Validate in a temporary state: a malformed final option cannot partially authorize a prefix/address.
    auto next = *this;
    if (!next.Process(packet, length, terminal, sender, now)) {
        return false;
    }
    *this = std::move(next);
    return true;
}

uint16_t NearlinkIpShareIpv6::U16(const uint8_t *p)
{
    return (uint16_t(p[0]) << 8) | p[1];
}

uint32_t NearlinkIpShareIpv6::U32(const uint8_t *p)
{
    return (uint32_t(U16(p)) << 16) | U16(p + 2);
}

NearlinkIpShareIpv6::Address NearlinkIpShareIpv6::Addr(const uint8_t *p)
{
    Address a{};
    std::copy(p, p + 16, a.begin());
    return a;
}

bool NearlinkIpShareIpv6::Zero(const Address &a)
{
    return a == Address{};
}

bool NearlinkIpShareIpv6::LinkLocal(const Address &a)
{
    return a[0] == 0xfe && a[1] == 0x80 && std::all_of(a.begin() + 2, a.begin() + 8, [](uint8_t b) { return b == 0; });
}

bool NearlinkIpShareIpv6::Unicast(const Address &a)
{
    return LinkLocal(a) || (a[0] & 0xe0) == 0x20 || (a[0] & 0xfe) == 0xfc;
}

bool NearlinkIpShareIpv6::Group(const Address &a, uint8_t group)
{
    Address expected{};
    expected[0] = 0xff;
    expected[1] = 2;
    expected[15] = group;
    return a == expected;
}

bool NearlinkIpShareIpv6::Solicited(const Address &a, const Address &target)
{
    Address expected{};
    expected[0] = 0xff;
    expected[1] = 2;
    expected[11] = 1;
    expected[12] = 0xff;
    std::copy(target.begin() + 13, target.end(), expected.begin() + 13);
    return a == expected;
}

uint64_t NearlinkIpShareIpv6::Until(uint64_t now, uint32_t lifetime)
{
    return lifetime == UINT32_MAX ? UINT64_MAX : now + lifetime;
}

uint32_t NearlinkIpShareIpv6::Sum(const uint8_t *p, size_t n, uint32_t sum)
{
    for (size_t i = 0; i < n; i += 2) {
        sum += (uint16_t(p[i]) << 8) | (i + 1 < n ? p[i + 1] : 0);
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return sum;
}

bool NearlinkIpShareIpv6::Checksum(const uint8_t *p, size_t length, size_t offset, uint8_t protocol)
{
    return Sum(p + offset, length - offset, Sum(p + 8, 32, protocol + length - offset)) == 0xffff;
}

NearlinkIpShareIpv6::Mapping *NearlinkIpShareIpv6::Find(const Address &a, bool terminal)
{
    for (auto &m : mappings_) {
        if (m.address == a && m.terminal == terminal) {
            return &m;
        }
    }
    return nullptr;
}

bool NearlinkIpShareIpv6::Known(const Address &a, bool terminal)
{
    auto m = Find(a, terminal);
    return m && !m->conflict;
}

bool NearlinkIpShareIpv6::Candidate(const Address &a, bool terminal, uint64_t now)
{
    if (!Unicast(a)) {
        return false;
    }
    if (Find(a, terminal)) {
        return true;
    }
    uint64_t preferred = UINT64_MAX, valid = UINT64_MAX;
    if (!LinkLocal(a)) {
        bool allowed = false;
        for (const auto &p : prefixes_) {
            if (std::equal(a.begin(), a.begin() + 8, p.address.begin())) {
                preferred = p.preferredUntil;
                valid = p.validUntil;
                allowed = valid > now;
                break;
            }
        }
        if (!allowed) {
            return false;
        }
    }
    if (now >= rateUntil_) {
        rateUntil_ = now + 60;
        candidates_ = 0;
    }
    if (candidates_ >= 16 ||
        std::count_if(mappings_.begin(), mappings_.end(), [](const Mapping &m) { return !m.confirmed; }) >= 16) {
        return false;
    }
    ++candidates_;
    mappings_.push_back({a, terminal, false, false, std::min(now + 60, valid), preferred, valid});
    return true;
}

bool NearlinkIpShareIpv6::Confirm(const Address &a, bool terminal)
{
    auto m = Find(a, terminal);
    if (!m || m->conflict) {
        return false;
    }
    auto other = Find(a, !terminal);
    if (other && (other->confirmed || !other->conflict)) {
        return false;
    }
    if (m->confirmed) {
        return true;
    }
    if (std::count_if(mappings_.begin(), mappings_.end(),
                      [terminal](const Mapping &v) { return v.terminal == terminal && v.confirmed; }) >= 8) {
        return false;
    }
    m->confirmed = true;
    return true;
}

bool NearlinkIpShareIpv6::ConfirmOwner(const Address &a, bool terminal, uint64_t now)
{
    if (!Candidate(a, terminal, now)) {
        return false;
    }
    auto owner = Find(a, terminal);
    auto other = Find(a, !terminal);
    owner->conflict = false;
    // A non-tentative local address or an authenticated NA wins over an
    // unconfirmed DAD candidate. An already confirmed owner still wins.
    if (other && !other->confirmed) {
        other->conflict = true;
    }
    return Confirm(a, terminal);
}

bool NearlinkIpShareIpv6::LearnPrefix(const uint8_t *option, uint64_t now)
{
    if (option[1] != 4 || option[2] != 64 || (option[3] & 0x40) == 0 || U32(option + 8) > U32(option + 4)) {
        return false;
    }
    Address a = Addr(option + 16);
    if (!Unicast(a) || LinkLocal(a) || !std::all_of(a.begin() + 8, a.end(), [](uint8_t v) { return v == 0; })) {
        return false;
    }
    auto it = std::find_if(prefixes_.begin(), prefixes_.end(), [&a](const Prefix &p) { return p.address == a; });
    if (it == prefixes_.end()) {
        if (prefixes_.size() == 4) {
            return false;
        }
        prefixes_.push_back({a, 0, 0});
        it = prefixes_.end() - 1;
    }
    it->preferredUntil = Until(now, U32(option + 8));
    it->validUntil = Until(now, U32(option + 4));
    for (auto &m : mappings_) {
        if (std::equal(a.begin(), a.begin() + 8, m.address.begin())) {
            m.preferredUntil = it->preferredUntil;
            // RFC 4862 5.5.3(e): unauthenticated RA cannot immediately invalidate a configured address.
            uint64_t remaining = m.validUntil > now ? m.validUntil - now : 0;
            uint32_t advertised = U32(option + 4);
            if (!m.confirmed || advertised > 7200 || advertised > remaining) {
                m.validUntil = it->validUntil;
            } else if (remaining > 7200) {
                m.validUntil = now + 7200;
            }
            if (!m.confirmed) {
                m.candidateUntil = std::min(m.candidateUntil, m.validUntil);
            }
        }
    }
    return true;
}

bool NearlinkIpShareIpv6::Options(const uint8_t *p, size_t n, size_t offset, uint8_t type, bool unspecified,
                                  const uint8_t sender[6], uint64_t now)
{
    bool linkOption = false;
    for (size_t i = offset; i < n;) {
        if (n - i < 2 || p[i + 1] == 0 || size_t(p[i + 1]) * 8 > n - i) {
            return false;
        }
        size_t bytes = size_t(p[i + 1]) * 8;
        if (p[i] == 1 || p[i] == 2) {
            if (bytes != 8 || linkOption || unspecified || p[i] != (type == 136 ? 2 : 1) ||
                std::memcmp(p + i + 2, sender, 6) != 0) {
                return false;
            }
            linkOption = true;
        } else if (type == 134 && p[i] == 3) {
            if (bytes != 32 || !LearnPrefix(p + i, now)) {
                return false;
            }
        } else if (type == 134 && p[i] == 25) {
            if (bytes < 24 || (p[i + 1] & 1) == 0 || bytes > 72) {
                return false;
            }
            for (size_t j = i + 8; j < i + bytes; j += 16) {
                if (!Unicast(Addr(p + j))) {
                    return false;
                }
            }
        } else if (type == 134 && p[i] == 5) {
            if (bytes != 8 || U32(p + i + 4) < 1280 || U32(p + i + 4) > 1500) {
                return false;
            }
        }
        i += bytes;
    }
    return true;
}

bool NearlinkIpShareIpv6::Process(const uint8_t *p, size_t n, bool terminal, const uint8_t sender[6], uint64_t now)
{
    if (!p || n < 40 || n > 1500 || p[0] >> 4 != 6 || U16(p + 4) != n - 40) {
        return false;
    }
    Address source = Addr(p + 8), destination = Addr(p + 24);
    size_t offset = 40, extensionBytes = 0, count = 0;
    uint8_t protocol = p[6];
    while (protocol == 0 || protocol == 60 || protocol == 43 || protocol == 51 || protocol == 44) {
        if (protocol == 43 || ++count > 8 || n - offset < 2) {
            return false;
        }
        if (protocol == 44) {
            // Fragments cannot promote an address. The kernel owns data reassembly.
            // Reject ND in the first/atomic fragment; later fragments never enter ND processing.
            if (n - offset <= 8 || extensionBytes + 8 > 256 || p[offset + 1] != 0 || (U16(p + offset + 2) & 6) != 0 ||
                ((p[offset + 3] & 1) && (n - offset - 8) % 8 != 0)) {
                return false;
            }
            if ((U16(p + offset + 2) & 0xfff8) == 0) {
                size_t first = offset + 8;
                uint8_t next = p[offset];
                while (next == 0 || next == 60 || next == 51) {
                    if (++count > 8 || n - first < 2) {
                        return false;
                    }
                    size_t bytes = next == 51 ? (size_t(p[first + 1]) + 2) * 4 : (size_t(p[first + 1]) + 1) * 8;
                    if (bytes > n - first || extensionBytes + 8 + bytes > 256) {
                        return false;
                    }
                    next = p[first];
                    first += bytes;
                    extensionBytes += bytes;
                }
                if (next == 43 || next == 44 ||
                    (next == 58 && (n - first < 4 || (p[first] >= 133 && p[first] <= 137)))) {
                    return false;
                }
            }
            auto mapping = Find(terminal ? source : destination, true);
            return mapping && mapping->confirmed && !mapping->conflict && Unicast(source) && Unicast(destination) &&
                   (!LinkLocal(source) || LinkLocal(destination));
        }
        size_t bytes = protocol == 51 ? (size_t(p[offset + 1]) + 2) * 4 : (size_t(p[offset + 1]) + 1) * 8;
        if (bytes > n - offset || extensionBytes + bytes > 256) {
            return false;
        }
        protocol = p[offset];
        offset += bytes;
        extensionBytes += bytes;
    }
    if (protocol == 58 && n - offset >= 4 && p[offset] >= 133 && p[offset] <= 137) {
        return ProcessNd(p, n, offset, terminal, sender, now);
    }
    return ProcessData(p, n, offset, protocol, terminal);
}

bool NearlinkIpShareIpv6::ProcessNd(const uint8_t *p, size_t n, size_t offset, bool terminal, const uint8_t sender[6],
                                    uint64_t now)
{
    Address source = Addr(p + 8), destination = Addr(p + 24);
    uint8_t type = p[offset];
    size_t fixed = type == 133 ? 8 : (type == 134 ? 16 : 24);
    if (type == 137 || n - offset < fixed || p[7] != 255 || p[offset + 1] != 0 || !Checksum(p, n, offset, 58)) {
        return false;
    }
    bool unspecified = Zero(source);
    if (!unspecified && !Unicast(source)) {
        return false;
    }
    if (type == 133) {
        if (!terminal || (!unspecified && !LinkLocal(source)) || !Group(destination, 2)) {
            return false;
        }
    } else if (type == 134) {
        if (terminal || !LinkLocal(source) || (!Group(destination, 1) && !Known(destination, true))) {
            return false;
        }
    } else {
        Address target = Addr(p + offset + 8);
        if (!Unicast(target)) {
            return false;
        }
        if (type == 135) {
            if (unspecified) {
                if (!Solicited(destination, target)) {
                    return false;
                }
            } else if (!Solicited(destination, target) && destination != target) {
                return false;
            }
        } else {
            if (unspecified || (!Group(destination, 1) && !Known(destination, !terminal)) ||
                (Group(destination, 1) && (p[offset + 4] & 0x40))) {
                return false;
            }
            // This Demo does not proxy NA. A tentative address must never emit NA.
            auto owner = Find(target, terminal);
            auto claimant = Find(target, !terminal);
            if (source != target || (!owner && (!claimant || claimant->confirmed)) ||
                !ConfirmOwner(target, terminal, now)) {
                return false;
            }
        }
    }
    if (!Options(p, n, offset + fixed, type, unspecified, sender, now)) {
        return false;
    }
    if (type == 135 && unspecified) {
        Address target = Addr(p + offset + 8);
        if (!Candidate(target, terminal, now)) {
            return false;
        }
        auto other = Find(target, !terminal);
        if (other) {
            Find(target, terminal)->conflict = true;
            if (!other->confirmed) {
                other->conflict = true;
            }
        }
        return true; // DAD records remain tentative, regardless of elapsed time.
    }
    if (type == 136) {
        auto other = Find(Addr(p + offset + 8), !terminal);
        if (other && !other->confirmed) {
            other->conflict = true;
        }
    }
    if (!unspecified && !Known(source, terminal)) {
        // RA authenticates router LLA; an RS authenticates only a candidate LLA.
        if ((type != 133 && type != 134) || !Candidate(source, terminal, now)) {
            return false;
        }
    }
    if (type == 134 && !Confirm(source, false)) {
        return false;
    }
    return true;
}

bool NearlinkIpShareIpv6::ProcessData(const uint8_t *p, size_t n, size_t offset, uint8_t protocol, bool terminal)
{
    Address source = Addr(p + 8), destination = Addr(p + 24);
    // A complete, checksummed upper-layer first packet is required before mapping promotion.
    auto sourceMap = Find(source, terminal);
    auto destinationMap = Find(destination, !terminal);
    if (terminal && (!sourceMap || sourceMap->conflict)) {
        return false;
    }
    if (!terminal && (!destinationMap || !destinationMap->confirmed || destinationMap->conflict)) {
        return false;
    }
    if (!Unicast(source) || !Unicast(destination)) {
        return false;
    }
    if (LinkLocal(source) && !LinkLocal(destination)) {
        return false;
    }
    if (terminal && !sourceMap->confirmed) {
        if (protocol == 17) {
            if (n - offset < 8 || U16(p + offset + 4) != n - offset || U16(p + offset + 6) == 0) {
                return false;
            }
        } else if (protocol == 6) {
            if (n - offset < 20 || (p[offset + 12] >> 4) < 5 || size_t(p[offset + 12] >> 4) * 4 > n - offset) {
                return false;
            }
        } else if (protocol == 58) {
            if (n - offset < 8 || (p[offset] != 128 && p[offset] != 129) || p[offset + 1] != 0) {
                return false;
            }
        } else {
            return false;
        }
        if (!Checksum(p, n, offset, protocol)) {
            return false;
        }
        if (sourceMap && !Confirm(source, terminal)) {
            return false;
        }
    }
    return true;
}
} // namespace OHOS::Nearlink
