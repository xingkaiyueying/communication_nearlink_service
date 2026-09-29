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
#ifndef NEARLINK_IPSHARE_STUB_H
#define NEARLINK_IPSHARE_STUB_H

#include <map>
#include <memory>
#include <utility>

#include "i_nearlink_ipshare.h"
#include "iremote_stub.h"
#include "nearlink_permission_item.h"

namespace OHOS::Nearlink {

class NearlinkIpShareStub : public IRemoteStub<INearlinkIpShare> {
public:
    NearlinkIpShareStub();
    ~NearlinkIpShareStub() override = default;
    int32_t OnRemoteRequest(uint32_t code, MessageParcel &data, MessageParcel &reply,
        MessageOption &option) override;

private:
    using Handler = int32_t (*)(NearlinkIpShareStub *, MessageParcel &, MessageParcel &);
    using HandlerWithPermission = std::pair<Handler, std::shared_ptr<NearLinkPermissionItem>>;

    static int32_t IsPeerSupportedInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply);
    static int32_t StartGatewayInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply);
    static int32_t StartTerminalInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply);
    static int32_t StopInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply);
    static int32_t GetStatusInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply);
    static int32_t RegisterObserverInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply);
    static int32_t UnregisterObserverInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply);

    static int32_t QueryNearlinkIpShareCapabilitiesInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply);
    static int32_t StartNearlinkGatewayWithModeInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply);
    static int32_t StartNearlinkTerminalWithModeInner(NearlinkIpShareStub *stub, MessageParcel &data, MessageParcel &reply);
    static int32_t UpdateValidatedAddressInner(NearlinkIpShareStub *, MessageParcel &, MessageParcel &);
    std::map<uint32_t, HandlerWithPermission> memberFuncMap_;
};

}  // namespace OHOS::Nearlink
#endif  // NEARLINK_IPSHARE_STUB_H
