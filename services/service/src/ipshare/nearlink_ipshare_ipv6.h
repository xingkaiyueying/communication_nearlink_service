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
#ifndef NEARLINK_IPSHARE_IPV6_H
#define NEARLINK_IPSHARE_IPV6_H

#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace OHOS::Nearlink {
// Owned by one authenticated channel; caller serializes access and supplies monotonic seconds.
// No ND replies are synthesized here: the owning kernel handles DAD and NUD.
class NearlinkIpShareIpv6 {
public:
    using Address = std::array<uint8_t, 16>;
    struct Mapping {
        Address address{};
        bool terminal{false};
        bool confirmed{false};
        bool conflict{false};
        uint64_t candidateUntil{0};
        uint64_t preferredUntil{0};
        uint64_t validUntil{0};
        uint64_t kernelUntil{0};
    };
    struct Prefix {
        Address address{};
        uint64_t preferredUntil{0};
        uint64_t validUntil{0};
    };
    void Reset();
    const std::vector<Mapping> &Mappings() const
    {
        return mappings_;
    }
    bool ApplyLocal(const Address &address, bool terminal, uint32_t flags, uint32_t preferred, uint32_t valid,
                    uint64_t now);
    bool ObserveKernelLocal(const Address &address, bool terminal, uint64_t now);
    bool LocalUsable(const Address &address, bool terminal, uint64_t now);
    static bool AddLayer2Option(std::vector<uint8_t> &packet, const uint8_t sender[6]);
    void Expire(uint64_t now);
    bool Authorize(const uint8_t *packet, size_t length, bool terminal, const uint8_t sender[6], uint64_t now);

private:
    static uint16_t U16(const uint8_t *p);
    static uint32_t U32(const uint8_t *p);
    static Address Addr(const uint8_t *p);
    static bool Zero(const Address &a);
    static bool LinkLocal(const Address &a);
    static bool Unicast(const Address &a);
    static bool Group(const Address &a, uint8_t group);
    static bool Solicited(const Address &a, const Address &target);
    static uint64_t Until(uint64_t now, uint32_t lifetime);
    static uint32_t Sum(const uint8_t *p, size_t n, uint32_t sum);
    static bool Checksum(const uint8_t *p, size_t length, size_t offset, uint8_t protocol);
    Mapping *Find(const Address &a, bool terminal);
    bool Known(const Address &a, bool terminal);
    bool Candidate(const Address &a, bool terminal, uint64_t now);
    bool Confirm(const Address &a, bool terminal);
    bool ConfirmOwner(const Address &a, bool terminal, uint64_t now);
    bool LearnPrefix(const uint8_t *option, uint64_t now);
    bool Options(const uint8_t *p, size_t n, size_t offset, uint8_t type, bool unspecified, const uint8_t sender[6],
                 uint64_t now);
    bool Process(const uint8_t *p, size_t n, bool terminal, const uint8_t sender[6], uint64_t now);
    bool ProcessNd(const uint8_t *p, size_t n, size_t offset, bool terminal, const uint8_t sender[6], uint64_t now);
    bool ProcessData(const uint8_t *p, size_t n, size_t offset, uint8_t protocol, bool terminal);
    std::vector<Mapping> mappings_;
    std::vector<Prefix> prefixes_;
    uint64_t rateUntil_{0};
    uint32_t candidates_{0};
};
} // namespace OHOS::Nearlink
#endif
