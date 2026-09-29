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
#ifndef NEARLINK_IPSHARE_CLIENT_H
#define NEARLINK_IPSHARE_CLIENT_H

#include <memory>
#include <string>

#include "nearlink_def.h"
#include "nearlink_ipshare_status.h"

namespace OHOS::Nearlink {

class NEARLINK_API NearlinkIpShareObserver {
public:
    virtual ~NearlinkIpShareObserver() = default;
    virtual void OnStatusChanged(const NearlinkIpShareStatus &status) = 0;
};

class NEARLINK_API NearlinkIpShareClient final {
public:
    static NearlinkIpShareClient &GetInstance();

    int32_t UpdateValidatedAddress(const NearlinkIpShareAddressEvidence &address) const;
    int32_t IsPeerSupported(const std::string &peerAddress, bool &supported) const;
    int32_t StartGateway(const std::string &peerAddress) const;
    int32_t StartTerminal(const std::string &gatewayAddress) const;
    int32_t Stop() const;
    int32_t GetStatus(NearlinkIpShareStatus &status) const;
    int32_t RegisterObserver(const std::shared_ptr<NearlinkIpShareObserver> &observer) const;
    int32_t UnregisterObserver() const;
    int32_t QueryNearlinkIpShareCapabilities(const std::string &peerAddress, NearlinkIpShareCapabilities &capabilities) const;
    int32_t StartNearlinkGatewayWithMode(const std::string &peerAddress, int32_t mode) const;
    int32_t StartNearlinkTerminalWithMode(const std::string &peerAddress, int32_t mode) const;


private:
    NearlinkIpShareClient();
    ~NearlinkIpShareClient();
    struct impl;
    std::shared_ptr<impl> pimpl_;
};

}  // namespace OHOS::Nearlink
#endif  // NEARLINK_IPSHARE_CLIENT_H
