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
#ifndef NEARLINK_IPSHARE_TUN_H
#define NEARLINK_IPSHARE_TUN_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <string>

namespace OHOS::Nearlink {

class NearlinkIpShareTun final {
public:
    using PacketCallback = std::function<void(const uint8_t *, uint16_t)>;

    NearlinkIpShareTun() = default;
    ~NearlinkIpShareTun();

    int32_t Open(const PacketCallback &callback);
    void Close();
    int32_t Write(const uint8_t *data, uint16_t length);
    bool IsOpen() const;
    static bool IsIpv6AddressUsable(const uint8_t address[16]);
    static bool ParseIpv6Evidence(const std::string &text, uint32_t index, uint8_t address[16]);

private:
    void ReadLoop();

    mutable std::mutex mutex_;
    int fd_ {-1};
    std::atomic_bool running_ {false};
    PacketCallback callback_;
    std::thread reader_;
};

}  // namespace OHOS::Nearlink
#endif  // NEARLINK_IPSHARE_TUN_H
