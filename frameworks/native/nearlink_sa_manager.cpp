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
#include "nearlink_sa_manager.h"

#include <thread>
#include <atomic>
#include <mutex>

#include "i_nearlink_host.h"
#include "nearlink_host.h"
#include "log.h"
#include "iservice_registry.h"
#include "system_ability_definition.h"

namespace OHOS {
namespace Nearlink {
namespace {
constexpr uint32_t INITIAL_ID = 1;
std::atomic_int32_t g_profileId = INITIAL_ID;
constexpr uint32_t SUBSCRIBE_SERVICE_TIMEOUT_MS = 50; // ms
const char *NEARLINK_HOST = "NearlinkHost";
}

NearlinkSaManager::NearlinkSaManager()
{
    nearlinkSa_ = sptr<NearlinkSa>::MakeSptr();
    SubscribeNearlinkSa();
}

NearlinkSaManager::~NearlinkSaManager()
{
    UnsubscribeNearlinkSa();
}

NearlinkSaManager &NearlinkSaManager::GetInstance()
{
    // C++11 static local variable initialization is thread-safe.
    static NearlinkSaManager saMgr;
    return saMgr;
}

void NearlinkSaManager::SubscribeNearlinkSa()
{
    sptr<ISystemAbilityManager> samgrProxy = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    NL_CHECK_RETURN(samgrProxy, "samgrProxy is nullptr.");
    int32_t ret = samgrProxy->SubscribeSystemAbility(NEARLINK_HOST_SYS_ABILITY_ID, nearlinkSa_);
    NL_CHECK_RETURN(ret == ERR_OK, "subscribe nearlink SA failed!");
}

void NearlinkSaManager::UnsubscribeNearlinkSa()
{
    sptr<ISystemAbilityManager> samgrProxy = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    NL_CHECK_RETURN(samgrProxy, "samgrProxy is nullptr.");
    int32_t ret = samgrProxy->UnSubscribeSystemAbility(NEARLINK_HOST_SYS_ABILITY_ID, nearlinkSa_);
    NL_CHECK_RETURN(ret == ERR_OK, "unsubscribe nearlink SA failed!");
}

sptr<IRemoteObject> NearlinkSaManager::GetRemoteHost()
{
    sptr<IRemoteObject> remote = nullptr;
    if (profileRemoteMap_.GetValue(NEARLINK_HOST, remote)) {
        return remote;
    }
    auto samgrProxy = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    NL_CHECK_RETURN_RET(samgrProxy, nullptr, "samgrProxy is nullptr.");

    remote = samgrProxy->CheckSystemAbility(NEARLINK_HOST_SYS_ABILITY_ID);
    if (remote == nullptr) {
        // The host SA may have been unloaded after boot. CheckSystemAbility does
        // not start an on-demand SA, so load it for a new IP share request.
        remote = samgrProxy->GetSystemAbility(NEARLINK_HOST_SYS_ABILITY_ID);
    }
    NL_CHECK_RETURN_RET(remote, nullptr, "remote is nullptr.");

    return remote;
}

sptr<IRemoteObject> NearlinkSaManager::GetRemoteProfile(const std::string &profileName)
{
    sptr<IRemoteObject> remote = nullptr;
    if (profileRemoteMap_.GetValue(profileName, remote)) {
        return remote;
    }

    auto remoteHost = GetRemoteHost();
    NL_CHECK_RETURN_RET(remoteHost, nullptr, "remoteHost is nullptr.");
    if (profileName == NEARLINK_HOST) {
        remote = remoteHost;
    } else {
        sptr<INearlinkHost> hostProxy = iface_cast<INearlinkHost>(remoteHost);
        NL_CHECK_RETURN_RET(hostProxy, nullptr, "hostProxy is nullptr");
        hostProxy->GetProfile(profileName, remote);
    }
    NL_CHECK_RETURN_RET(remote, nullptr, "remote is nullptr");
    profileRemoteMap_.EnsureInsert(profileName, remote);
    return remote;
}

void NearlinkSaManager::OnServiceStarted()
{
    HILOGI("Nearlink service started.");
    std::unique_lock<std::mutex> lock(serviceStateMutex_);
    profileIdFuncMap_.Iterate([this](const int32_t id, std::shared_ptr<NearlinkRegisterInfo> &info) -> void {
        auto remote = GetRemoteProfile(info->profileName_);
        if (info->serviceStartedFunc_) {
            HILOGI("execute serviceStartedFunc, id(%{public}d), profileName(%{public}s)",
                id, info->profileName_.c_str());
            info->serviceStartedFunc_(remote);
        }
    });
    serviceState_ = ServiceState::SERVICE_STATE_STARTED;
    cvfull_.notify_all();
}

void NearlinkSaManager::OnServiceStopped()
{
    HILOGI("Nearlink service stopped.");
    std::unique_lock<std::mutex> lock(serviceStateMutex_);
    profileRemoteMap_.Clear();
    profileIdFuncMap_.Iterate([this](const int32_t id, std::shared_ptr<NearlinkRegisterInfo> &info) -> void {
        if (info->serviceStoppedFunc_) {
            HILOGI("execute serviceStoppedFunc, id(%{public}d, profileName(%{public}s)",
                id, info->profileName_.c_str());
            info->serviceStoppedFunc_();
        }
    });
    serviceState_ = ServiceState::SERVICE_STATE_STOPPED;
    cvfull_.notify_all();
}

void NearlinkSaManager::NearlinkSa::OnAddSystemAbility(
    int32_t systemAbilityId, const std::string &deviceId)
{
    switch (systemAbilityId) {
        case NEARLINK_HOST_SYS_ABILITY_ID: {
            NearlinkSaManager::GetInstance().OnServiceStarted();
            break;
        }
        default:
            HILOGE("unexpected sysabilityId(%{public}d)", systemAbilityId);
            break;
    }
}

void NearlinkSaManager::NearlinkSa::OnRemoveSystemAbility(
    int32_t systemAbilityId, const std::string &deviceId)
{
    switch (systemAbilityId) {
        case NEARLINK_HOST_SYS_ABILITY_ID: {
            NearlinkSaManager::GetInstance().OnServiceStopped();
            break;
        }
        default:
            HILOGE("unhandled sysabilityId(%{public}d)", systemAbilityId);
            break;
    }
    return;
}

void NearlinkSaManager::OnStateChanged(const int transport, const int status)
{
    HILOGD("status: %{public}d,", status);
    if (transport == static_cast<int>(SleTransport::ADAPTER_SLE) &&
        status == static_cast<int>(SleStateID::STATE_TURN_OFF)) {
        profileIdFuncMap_.Iterate([this](const int32_t id, std::shared_ptr <NearlinkRegisterInfo> &info) -> void {
            sptr <IRemoteObject> remote = GetRemoteProfile(info->profileName_);
            if (info->stateOffFunc_) {
                info->stateOffFunc_(remote);
            }
        });
    } else if (transport == static_cast<int>(SleTransport::ADAPTER_SLE) &&
               status == static_cast<int>(SleStateID::STATE_TURN_ON)) {
        profileIdFuncMap_.Iterate([this](const int32_t id, std::shared_ptr <NearlinkRegisterInfo> &info) -> void {
            sptr <IRemoteObject> remote = GetRemoteProfile(info->profileName_);
            if (info->stateOnFunc_ && remote != nullptr) {
                info->stateOnFunc_(remote);
            }
        });
    }
}

int32_t NearlinkSaManager::AllocateProfileId()
{
    std::shared_ptr<NearlinkRegisterInfo> info = nullptr;
    while (profileIdFuncMap_.GetValue(g_profileId, info)) {
        g_profileId++;
        if (g_profileId == INT32_MAX) {
            HILOGW("The profileId reaches the maximum value.");
            g_profileId = INITIAL_ID;
        }
    }
    return g_profileId;
}

int32_t NearlinkSaManager::RegisterFunc(std::shared_ptr<NearlinkRegisterInfo> info)
{
    NL_CHECK_RETURN_RET(info, INVALID_PROFILE_ID, "info is nullptr.");

    std::unique_lock<std::mutex> lock(serviceStateMutex_);
    int32_t profileId = AllocateProfileId();
    HILOGI("profileName(%{public}s), profileId(%{public}d), serviceState_(%{public}d)",
        info->profileName_.c_str(), profileId, serviceState_);
    profileIdFuncMap_.EnsureInsert(profileId, info);

    if (serviceState_ == ServiceState::SERVICE_STATE_NOT_SUBSCRIBED) {
        // 首次订阅，等待服务启动事件
        HILOGI("waiting for service state event.");
        auto wait = cvfull_.wait_for(lock, std::chrono::milliseconds(SUBSCRIBE_SERVICE_TIMEOUT_MS), [this] {
            return (serviceState_ == ServiceState::SERVICE_STATE_STARTED ||
                serviceState_ == ServiceState::SERVICE_STATE_STOPPED);
        });
        HILOGI("received serviceState_(%{public}d)", serviceState_);
        if (!wait) {
            HILOGE("subscribe nearlink service timeout, maybe nearlink service is stopped!");
            serviceState_ = ServiceState::SERVICE_STATE_STOPPED;
        }
    } else if (serviceState_ == ServiceState::SERVICE_STATE_STARTED) {
        // 已经订阅过服务启动事件, 服务已启动
        sptr<IRemoteObject> remote = GetRemoteProfile(info->profileName_);
        NL_CHECK_RETURN_RET(remote, profileId, "remote is nullptr.");
        if (info->serviceStartedFunc_) {
            HILOGI("execute serviceStartedFunc, profileId(%{public}d), profileName(%{public}s)",
                profileId, info->profileName_.c_str());
            info->serviceStartedFunc_(remote);
        }
        if (info->stateOnFunc_ && NearlinkHost::GetInstance().IsSleAvailableToCaller()) {
            info->stateOnFunc_(remote);
        }
    } else {
        // 已经订阅过服务启动事件, 服务已停止
        HILOGW("service is stopped, serviceState_(%{public}d)", serviceState_);
    }
    return profileId;
}

void NearlinkSaManager::DeregisterFunc(int32_t profileId)
{
    std::unique_lock<std::mutex> lock(serviceStateMutex_);
    HILOGI("profileId(%{public}d)", profileId);
    profileIdFuncMap_.Erase(profileId);
}
} // namespace Nearlink
} // namespace OHOS