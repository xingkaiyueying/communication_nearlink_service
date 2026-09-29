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
#ifndef I_NEARLINK_IPSHARE_H
#define I_NEARLINK_IPSHARE_H

#include <string>

#include "i_nearlink_ipshare_observer.h"
#include "iremote_broker.h"
#include "nearlink_ipshare_status.h"

namespace OHOS::Nearlink {

inline const std::string PROFILE_IPSHARE_SERVER = "NearlinkIpShare";

class INearlinkIpShare : public IRemoteBroker {
public:
    DECLARE_INTERFACE_DESCRIPTOR(u"ohos.ipc.INearlinkIpShare");

    virtual int32_t IsPeerSupported(const std::string &peerAddress, bool &supported) = 0;
    virtual int32_t StartGateway(const std::string &peerAddress) = 0;
    virtual int32_t StartTerminal(const std::string &gatewayAddress) = 0;
    virtual int32_t Stop() = 0;
    virtual int32_t GetStatus(NearlinkIpShareStatus &status) = 0;
    virtual int32_t RegisterObserver(const sptr<INearlinkIpShareObserver> &observer) = 0;
    virtual int32_t UnregisterObserver() = 0;
    virtual int32_t QueryNearlinkIpShareCapabilities(const std::string &peerAddress, NearlinkIpShareCapabilities &capabilities) = 0;
    virtual int32_t StartNearlinkGatewayWithMode(const std::string &peerAddress, int32_t mode) = 0;
    virtual int32_t StartNearlinkTerminalWithMode(const std::string &peerAddress, int32_t mode) = 0;

    virtual int32_t UpdateValidatedAddress(const NearlinkIpShareAddressEvidence &address) = 0;
};

}  // namespace OHOS::Nearlink
#endif  // I_NEARLINK_IPSHARE_H
