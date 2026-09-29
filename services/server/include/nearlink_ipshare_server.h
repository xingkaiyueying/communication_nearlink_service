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
#ifndef NEARLINK_IPSHARE_SERVER_H
#define NEARLINK_IPSHARE_SERVER_H

#include "nearlink_ipshare_stub.h"

namespace OHOS::Nearlink {

class NearlinkIpShareServer final : public NearlinkIpShareStub {
public:
    int32_t UpdateValidatedAddress(const NearlinkIpShareAddressEvidence &address) override;
    int32_t IsPeerSupported(const std::string &peerAddress, bool &supported) override;
    int32_t StartGateway(const std::string &peerAddress) override;
    int32_t StartTerminal(const std::string &gatewayAddress) override;
    int32_t Stop() override;
    int32_t GetStatus(NearlinkIpShareStatus &status) override;
    int32_t RegisterObserver(const sptr<INearlinkIpShareObserver> &observer) override;
    int32_t UnregisterObserver() override;
    int32_t QueryNearlinkIpShareCapabilities(const std::string &peerAddress, NearlinkIpShareCapabilities &capabilities) override;
    int32_t StartNearlinkGatewayWithMode(const std::string &peerAddress, int32_t mode) override;
    int32_t StartNearlinkTerminalWithMode(const std::string &peerAddress, int32_t mode) override;

};

}  // namespace OHOS::Nearlink
#endif  // NEARLINK_IPSHARE_SERVER_H
