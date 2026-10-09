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

#include "nearlink_sle_datatransfer_service.h"
#include "nearlink_ipshare_channel.h"
#include "nearlink_ipshare_service.h"
#include "SleInterfaceAdapterSub.h"
#include "SleInterfaceManager.h"
#include "SleControllerService.h"
#include "IcceService.h"
#include "PortService.h"
#include "nearlink_safe_map.h"
#include "ThreadUtil.h"
#include "log_util.h"
#include "ipc_skeleton.h"
#include "nearlink_dft_exception.h"
#include "nearlink_permission_manager.h"
#include "parameters.h"
#include "cm_api.h"
#include "nearlink_socket_manager.h"
#include "nearlink_datatransfer_parcel.h"
#include "SleFeature.h"
#include <future>
#include "nearlink_timer.h"
#include "nearlink_verification_manager.h"
#ifdef RES_SCHED_SUPPORT
#include "res_sched_client.h"
#include "res_type.h"
#include "system_ability_definition.h"
#include "nearlink_freeze_utils.h"
#endif
#include "SleRemoteDeviceAdapter.h"
#include "nlstk_public_define_ext.h"
#include "nlstk_api_type_ext.h"

#include "transport_errno.h"
#include "transport_internal.h"

namespace OHOS::Nearlink {
namespace {
constexpr uint16_t PORT_DEFAULT = 30300;
constexpr uint16_t PORT_MAX = 40959;
constexpr uint16_t CAR_PORT = 40960;
const char* const STANDARD_UUID_ICCE = "060D";

constexpr uint8_t TCID_SLE_DEFAULT = 0x1F;
constexpr uint16_t PORT_CONN_WAIT_TIMEOUT = 11000; // 等待port连接结果超时间, 11000ms
constexpr uint16_t DATATRANSFER_SLE_CONN_EVENT_IFS =  125;                // 链路调度间隔, 125us
constexpr uint16_t DATATRANSFER_SLE_CONN_SUPERVISION_TIMEOUT =  1000;     // 超时时间10s
constexpr uint16_t DATATRANSFER_QOSM_SLE_CONN_TIME_UNIT =  4;             // 系统调度时隙, 125us
const int INVALID_FD = -1;
constexpr uint16_t DEFAULT_CAR_MTU = UINT16_MAX;    // 车钥匙基础模式默认MTU值为65535
#ifdef RES_SCHED_SUPPORT
const char* SA_NAME = "nearlink_service";
constexpr uint16_t RSS_THAW_TIMEOUT = 20000; // 等待RSS唤醒时间超时, 20000ms
#endif
}
struct SleDataTransferService::impl {
    impl();
    ~impl();

    class PortConnectInfo {
    public:
        std::unordered_map<uint16_t, DataTransferConnectionParams> appConnectReqMap_{}; // 主动连接请求参数
        std::shared_ptr<NearlinkTimer> appConnectTimer_ = nullptr; // 主动连接请求定时器
    };

    class PortServiceObserver final: public PortObserver {
    public:
        ~PortServiceObserver() = default;

        void OnConnectionStateChanged(const RawAddress &deviceAddress, int state, int oldState) override
        {
            HILOGD("dataTransfer status addr:%{public}s,new:%{public}d,old:0x%{public}d",
                GET_ENCRYPT_ADDR(deviceAddress), state, oldState);
            std::string addr = deviceAddress.GetAddress();
            DoInDataTransferThread([addr, state, oldState]() {
                SleDataTransferService::GetInstance().GetRemotePortByConnectionState(addr, state, oldState);
            });
        }
    };

    /**
     * @brief insert app connect params.
     *
     * @return
     */
    void InsertAppConnectParamMapping(
        const std::string &uuid, uint16_t port, uint64_t tokenId, uint32_t uid, uint32_t pid);

    /**
     * @brief update app connect params.
     *
     * @return
     */
    bool UpdateAppConnectParamMapping(uint16_t port, const AppConnectParamMapping &appConnectParamMapping);

    /**
     * @brief get app connect params.
     *
     * @return
     */
    bool GetAppConnectParamMapping(uint16_t port, std::string addr, AppConnectParamMapping &appConnectParamMapping);

    /**
     * @brief delete app connect params.
     *
     * @return
     */
    void DeleteAppConnectParamMapping(uint16_t port);

    /**
     * @brief find uuid.
     *
     * @return uuid
     */
    std::string FindUuidByPort(uint16_t port);

    /**
     * @brief find uid.
     *
     * @return uid
     */
    int32_t FindUidByPort(uint16_t port);

    /**
     * @brief find pid.
     *
     * @return pid
     */
    int32_t FindPidByPort(uint16_t port);

    /**
     * @brief get existed port.
     * @param uuid uuid
     * @param tokenId tokenId
     * @return port
     */
    uint16_t FindPortByUuidAndTokenId(const std::string &uuid, uint64_t tokenId);

    /**
     * @brief clear appConnectTimer and appConnectReq by address.
     *
     * @return
     */
    void ClearConnectReqAndTimer(std::string addr);

    bool CheckUuidIsConnect(const std::string &uuid);

    void ConnectCarReqStateChanged(const std::string addr);

#ifdef RES_SCHED_SUPPORT
    void NotifyRssAppStateChanged(uint32_t tokenId, int32_t uid, bool isFreeze);
#endif

    /// sle remote device observer
    class SleDataTransferAcbCallback;
    std::unique_ptr<SleDataTransferAcbCallback> sleDataTransferAcbObserverImp_ = nullptr;
    PortServiceObserver dataTransferPortObserver_;
    std::shared_ptr<ISleDataTransferServiceCallback> callback_ = nullptr;
    NearlinkSafeMap<uint16_t, std::shared_ptr<SleDataTransferCache>> appConnectParamMap_{};  // 应用connect后参数缓存
    // 缓存主动发起连接请求的连接参数
    std::unique_ptr<PortSocketManager> portSocketManager_ = nullptr;  // 数传socket管理
    std::unordered_map<std::string, std::shared_ptr<PortConnectInfo>> appConnectInfo_{}; // 应用connect的请求缓存
    std::shared_ptr<NearlinkTimer> cancelRssTimer_ = nullptr; // 取消豁免定时器
    SLE_DISALLOW_COPY_AND_ASSIGN(impl);
};

class SleDataTransferService::impl::SleDataTransferAcbCallback : public ISlePeripheralCallback {
public:
    SleDataTransferAcbCallback(SleDataTransferService::impl *impl) : impl_(impl) {};
    ~SleDataTransferAcbCallback() override = default;

    void OnAcbStateChanged(const RawAddress &device, int32_t state, int reason) override
    {
        HILOGD("addr: %{public}s, state: %{public}d, reason: 0x%{public}x", GET_ENCRYPT_ADDR(device), state, reason);
        NL_CHECK_RETURN_LOGD(state == static_cast<int>(SleConnState::SLE_CONNECTION_STATE_DISCONNECTED), "ignore");
        std::string addr = device.GetAddress();
        NearlinkIpShareService::GetInstance().OnPeerDisconnected(addr);
        DoInDataTransferThread([this, addr]() {
            impl_->ConnectCarReqStateChanged(addr);
        });
    }

    void OnConnectionStateChanged(const RawAddress &device, int32_t state, int32_t preState, int32_t reason) override
    {
        HILOGI("device: %{public}s, state %{public}d to %{public}d", GET_ENCRYPT_ADDR(device), preState, state);
        if (state == static_cast<int>(SleConnectState::DISCONNECTED)) {
            auto sleAdapter = static_cast<SleInterfaceAdapterSub *>(
                SleInterfaceManager::GetInstance()->GetAdapter(ADAPTER_SLE));
            NL_CHECK_RETURN(sleAdapter, "sleAdapter null");
            sleAdapter->DelConnFrameType(device.GetAddress());
        }
        NL_CHECK_RETURN_LOGD(state == static_cast<int>(SleConnectState::CONNECTED), "not connect");
        std::string addr = device.GetAddress();
        std::vector<DataTransferConnectionParams> tempVec;
        impl_->appConnectParamMap_.Iterate(
            [&tempVec, addr](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> void {
                for (const auto &val : value->GetPortConnects()) {
                    if (value->GetUuid() == STANDARD_UUID_ICCE && val.address == addr &&
                        val.state != static_cast<int32_t>(SleConnectState::CONNECTED)) {
                        HILOGI("acb connect: %{public}d", key);
                        DataTransferConnectionParams temp;
                        temp.SetUuid(value->GetUuid());
                        temp.SetAddress(val.address);
                        temp.SetPort(key);
                        tempVec.push_back(temp);
                    }
                }
            });
        for (auto &item : tempVec) {
            SleDataTransferService::GetInstance().ConnectCarChannelAfterAcb(item);
        }
    }

private:
    SleDataTransferService::impl *impl_ = nullptr;
    NEARLINK_DISALLOW_COPY_AND_ASSIGN(SleDataTransferAcbCallback);
};

SleDataTransferService::impl::impl()
{
    sleDataTransferAcbObserverImp_ = std::make_unique<SleDataTransferAcbCallback>(this);
    portSocketManager_ = std::make_unique<PortSocketManager>();
}

SleDataTransferService::impl::~impl()
{}

void SleDataTransferService::impl::InsertAppConnectParamMapping(const std::string &uuid, uint16_t port,
    uint64_t tokenId, uint32_t uid, uint32_t pid)
{
    HILOGD("enter");
    std::shared_ptr<SleDataTransferCache> cache = std::make_shared<SleDataTransferCache>();
    cache->SetUuid(uuid);
    cache->SetTokenId(tokenId);
    cache->SetUid(uid);
    cache->SetPid(pid);
    appConnectParamMap_.EnsureInsert(port, cache);
    HILOGI("After insert appConnectParamMap_ size = %{public}d", appConnectParamMap_.Size());
}

bool SleDataTransferService::impl::UpdateAppConnectParamMapping(uint16_t port, const AppConnectParamMapping &temp)
{
    HILOGD("update");
    auto updateCache = [&temp](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> void {
        value->SetPortConnects(temp);
    };
    return appConnectParamMap_.GetValueAndOpt(port, updateCache);
}

bool SleDataTransferService::impl::GetAppConnectParamMapping(uint16_t port, std::string addr,
    AppConnectParamMapping &temp)
{
    bool hasParam = false;
    auto getConnects = [&temp, &hasParam, addr](uint16_t key,
        std::shared_ptr<SleDataTransferCache> value) -> void {
        hasParam = value->GetAppConnectParamByAddr(addr, temp);
        NL_CHECK_RETURN(hasParam, "can not find appConnectParam by addr");
    };
    bool has = appConnectParamMap_.GetValueAndOpt(port, getConnects);
    return has && hasParam;
}

void SleDataTransferService::impl::DeleteAppConnectParamMapping(uint16_t port)
{
    HILOGD("enter");
    appConnectParamMap_.Erase(port);
    HILOGI("After delete appConnectParamMap_ size = %{public}d", appConnectParamMap_.Size());
}

std::string SleDataTransferService::impl::FindUuidByPort(uint16_t port)
{
    std::string res;
    std::shared_ptr<SleDataTransferCache> cache = nullptr;
    if (appConnectParamMap_.GetValue(port, cache)) {
        res = cache->GetUuid();
    }
    return res;
}

int32_t SleDataTransferService::impl::FindPidByPort(uint16_t port)
{
    int32_t res = 0;
    std::shared_ptr<SleDataTransferCache> cache = nullptr;
    if (appConnectParamMap_.GetValue(port, cache)) {
        res = cache->GetPid();
    }
    return res;
}

int32_t SleDataTransferService::impl::FindUidByPort(uint16_t port)
{
    int32_t res = 0;
    std::shared_ptr<SleDataTransferCache> cache = nullptr;
    if (appConnectParamMap_.GetValue(port, cache)) {
        res = cache->GetUid();
    }
    return res;
}

uint16_t SleDataTransferService::impl::FindPortByUuidAndTokenId(const std::string &uuid, uint64_t tokenId)
{
    uint16_t srcPort = 0;
    auto isCreated = [&uuid, &srcPort, tokenId](uint16_t key, std::shared_ptr<SleDataTransferCache> val) -> bool {
        bool isUuidMatch = (uuid == val->GetUuid());
        bool isTokenIdMatch = (tokenId == val->GetTokenId());
        bool isMatch = (uuid == STANDARD_UUID_ICCE ? (isUuidMatch && isTokenIdMatch) : isUuidMatch);
        if (isMatch) {
            srcPort = key;
            return true;
        }
        return false;
    };
    appConnectParamMap_.Find(isCreated);
    return srcPort;
}

bool SleDataTransferService::impl::CheckUuidIsConnect(const std::string &uuid)
{
    auto hasUuidConnect = [&uuid](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> bool {
        if (uuid == value->GetUuid() && value->HasAppConnect()) {
            return true;
        }
        return false;
    };
    return appConnectParamMap_.Find(hasUuidConnect);
}

void SleDataTransferService::impl::ConnectCarReqStateChanged(const std::string addr)
{
    std::vector<DataTransferConnectionParams> tempVec;
    // 车钥匙断开port业务, 不会销毁通道和回调断开portporfile, 仅回调acb状态, 需要将连接状态置为断开状态
    appConnectParamMap_.Iterate(
        [&tempVec, addr](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> void {
            for (const auto &val : value->GetPortConnects()) {
                if (value->GetUuid() == STANDARD_UUID_ICCE &&
                    val.state != static_cast<int32_t>(SleConnectState::DISCONNECTED) && val.address == addr) {
                    HILOGI("acb disconnect port: %{public}d", key);
                    DataTransferConnectionParams temp;
                    temp.SetUuid(value->GetUuid());
                    temp.SetAddress(val.address);
                    temp.SetPort(key);
                    temp.SetState(static_cast<int32_t>(SleConnectState::DISCONNECTED));
                    tempVec.push_back(temp);
                }
            }
        });
    NL_CHECK_RETURN(callback_, "callback_ null");
    auto func = [&addr](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> void {
        value->UpdateStateByAddr(addr, static_cast<int32_t>(SleConnectState::DISCONNECTED));
    };
    for (auto& item : tempVec) {
        if (appConnectParamMap_.GetValueAndOpt(item.GetPort(), func)) {
            HILOGI("update to disconnected");
        }
        callback_->OnConnectionStateChanged(item, INVALID_FD);
    }
}

void SleDataTransferService::impl::ClearConnectReqAndTimer(std::string addr)
{
    auto item = appConnectInfo_.find(addr);
    if (item != appConnectInfo_.end()) {
        HILOGI("delete connect timer, addr: %{public}s", GetEncryptAddr(addr).c_str());
        if (item->second->appConnectTimer_ != nullptr) {
            item->second->appConnectTimer_->Stop();
            item->second->appConnectTimer_ = nullptr;
        }
        appConnectInfo_.erase(addr);
    }
}

#ifdef RES_SCHED_SUPPORT
void SleDataTransferService::impl::NotifyRssAppStateChanged(uint32_t tokenId, int32_t uid, bool isFreeze)
{
    std::string bundleName = NearLinkPermissionManager::GetHapBundleName(tokenId);
    uint32_t type = ResourceSchedule::ResType::RES_TYPE_SA_CONTROL_APP_EVENT;
    int64_t status = isFreeze ? ResourceSchedule::ResType::SaControlAppStatus::SA_STOP_APP
                              : ResourceSchedule::ResType::SaControlAppStatus::SA_START_APP;
    std::unordered_map<std::string, std::string> payload;
    // saID+saName 必选
    payload.emplace("saId", std::to_string(NEARLINK_HOST_SYS_ABILITY_ID));
    payload.emplace("saName", std::string(SA_NAME));
    // uid+bundleName 可选
    payload.emplace("uid", std::to_string(uid));
    payload.emplace("bundleName", bundleName);
    ResourceSchedule::ResSchedClient::GetInstance().ReportData(type, status, payload);
}
#endif

SleInterfaceDataTransfer &SleInterfaceDataTransfer::GetInstance()
{
    return SleDataTransferService::GetInstance();
}

SleDataTransferService &SleDataTransferService::GetInstance()
{
    static SleDataTransferService instance;
    return instance;
}

SleDataTransferService::SleDataTransferService() : pimpl(std::make_unique<SleDataTransferService::impl>())
{
    HILOGI("construct");
    RegisterSleDataTransferCallbackToStack();
    PortService *portService = PortService::GetPortService();
    NL_CHECK_RETURN(portService, "construct PortService empty");
    portService->RegisterObserver(pimpl->dataTransferPortObserver_);
}

SleDataTransferService::~SleDataTransferService()
{
    HILOGI("~SleDataTransferService");
    DeregisterCallback();
    PortService *portService = PortService::GetPortService();
    NL_CHECK_RETURN(portService, "destructor PortService empty");
    portService->DeregisterObserver(pimpl->dataTransferPortObserver_);
}

void SleDataTransferService::RegisterSleDataTransferServiceCallback(
    std::shared_ptr<ISleDataTransferServiceCallback> callback)
{
    NL_CHECK_RETURN(callback, "callback come null");
    NL_CHECK_RETURN(!pimpl->callback_, "already register cb");
    pimpl->callback_ = callback;

    SleInterfaceAdapterSub *sleService = static_cast<SleInterfaceAdapterSub *>
    (SleInterfaceManager::GetInstance()->GetAdapter(SleTransport::ADAPTER_SLE));
    NL_CHECK_RETURN(sleService, "sleService invalid.");
    sleService->RegisterSlePeripheralCallback(*pimpl->sleDataTransferAcbObserverImp_);
}

void SleDataTransferService::DeregisterSleDataTransferServiceCallback()
{
    pimpl->callback_ = nullptr;
    SleInterfaceAdapterSub *sleService = static_cast<SleInterfaceAdapterSub *>
    (SleInterfaceManager::GetInstance()->GetAdapter(SleTransport::ADAPTER_SLE));
    NL_CHECK_RETURN(sleService, "sleService invalid.");
    sleService->DeregisterSlePeripheralCallback(*pimpl->sleDataTransferAcbObserverImp_.get());
}

uint16_t SleDataTransferService::CreatePort(const std::string &uuid)
{
    HILOGD("enter");
    std::promise<uint16_t> promise;
    uint64_t tokenId = IPCSkeleton::GetCallingFullTokenID();
    int32_t uid = IPCSkeleton::GetCallingUid();
    int32_t pid = IPCSkeleton::GetCallingPid();
    DoInDataTransferThread([this, uuid, tokenId, uid, pid, &promise]() {
        uint16_t res = CreatePortInner(uuid, tokenId, uid, pid);
        promise.set_value(res);
    });
    uint16_t srcPort = promise.get_future().get();
    HILOGI("port: %{public}u;", srcPort);
    return srcPort;
}

uint16_t SleDataTransferService::CreatePortInner(
    const std::string &uuid, const uint64_t tokenId, const uint32_t uid, const uint32_t pid)
{
    uint16_t srcPort = pimpl->FindPortByUuidAndTokenId(uuid, tokenId);
    NL_CHECK_RETURN_RET(srcPort == 0, srcPort, "repeat create port");

    // 1、生成本地端口, 2字节数字范围0到65535, 数传业务从30300开始取
    for (uint16_t i = PORT_DEFAULT; i <= PORT_MAX; i++) {
        if (ports_.find(i) == ports_.end()) {
            srcPort = i;
            ports_.insert(srcPort);
            break;
        }
    }

    if (uuid != STANDARD_UUID_ICCE) {
        PortService *portService = PortService::GetPortService();
        NL_CHECK_RETURN_RET(portService, srcPort, "PortService empty");
        // 2、根据生成的端口去addService
        portService->AddPortByUuid((Uuid::ConvertFromString(uuid)).Uuid::ConvertTo128Bits(), srcPort);
    }

    HILOGI("port: %{public}u; current active ports count: %{public}zu", srcPort, ports_.size());
    pimpl->InsertAppConnectParamMapping(uuid, srcPort, tokenId, uid, pid);
    DftReportDtfrStatisPortInfo(NearLinkPermissionManager::GetCallingName(), DTFR_CREATE_PORT);
    return srcPort;
}

int SleDataTransferService::GetTransferState(uint16_t portId,const std::string &address)
{
    int transState = 0;
    int preTransState = 0;
    int stateRet = SleInterfaceDataTransfer::GetInstance().GetConnectionTransferState(portId, address, transState,
        preTransState);
    NL_CHECK_RETURN_RET(stateRet == 0, stateRet, "Failed to get connection transfer state.");
    if (transState != TransferState::TRANSFER_AVAILABLE) {
        if (transState != preTransState) {
            HILOGW("tcid channel is busy. transState=%{public}d, preTransSate=%{public}d, portId=%{public}d",
                transState, preTransState, portId);
        }
    }
    return transState;
}

bool SleDataTransferService::ReceivedData(std::shared_ptr<InputStream> inputStream, uint16_t portId,
    const std::string &address)
{
    int state = GetTransferState(portId, address);
    NL_CHECK_RETURN_RET(state != TransferState::TRANSFER_FAIL, false, "transfer state err");
    if (state == TransferState::TRANSFER_BUSY) {
        return true;
    }
    HILOGD("server read_data func start.");
    while (true) {
        const int packageLen = sizeof(size_t);
        uint8_t buf[packageLen];
        (void)memset_s(buf, sizeof(buf), 0, sizeof(buf));
        int ret = inputStream->Read(buf, sizeof(buf));
        NL_CHECK_RETURN_RET(ret != 0, false, "fd disconnected err");
        if (ret != packageLen) {
            break;
        }
        size_t pLen = *reinterpret_cast<const size_t*>(buf);
        pLen -= packageLen;
        HILOGD("data size : %{public}zu", pLen);
        NL_CHECK_RETURN_RET(pLen > 0 && pLen < SOCKET_BUFFER_SIZE, false, "input stream data len is invalid");
        uint8_t pBuf[pLen];
        (void)memset_s(pBuf, sizeof(pBuf), 0, sizeof(pBuf));
        int res = inputStream->Read(pBuf, sizeof(pBuf));
        NL_CHECK_RETURN_RET(ret != 0, false, "fd disconnected err");
        NL_CHECK_RETURN_RET(ret == packageLen, false, "data len err");
        std::shared_ptr<DataTransferDataParams> result = std::make_shared<DataTransferDataParams>();
        NearlinkDataTransferDataParams::DeserializeData(pBuf, pLen, *result);
        result->address_ = address;
        std::promise<int> promise;
        DoInDataTransferThread([this, result, &promise]() {
            int wRet = WriteData(*result);
            promise.set_value(wRet);
        });
        int wRet = promise.get_future().get();
        if (wRet == TRANS_BUSY) {
            HILOGW("stack send queue is busy");
            break;
        } else if (wRet == TRANS_SUCCESS) {
            HILOGD("WriteTransferData success");
        }
        else {
            HILOGE("WriteTransferData failed");
        }
    }
    return true;
}

void SleDataTransferService::DestroyPort(const std::string &uuid, uint16_t port)
{
    std::promise<void> promise;
    DoInDataTransferThread([this, uuid, port, &promise]() -> void {
        DestroyPortInner(uuid, port);
        promise.set_value();
    });
    promise.get_future().get();
    pimpl->portSocketManager_->DestroyPort(port); // 销毁socket
}

void SleDataTransferService::ReleasePortChannel(const std::string &address, uint8_t tcid) const
{
    QOSM_TransChannelReleaseParams_S param;
    (void)memset_s(&param, sizeof(param), 0x00, sizeof(param));
    RawAddress addr(address);
    addr.ConvertToUint8(param.addr.addr);
    param.tcid = tcid;
    NLSTK_ERRCODE ret = QOSM_TransChannelDestroy(&param);
    NL_CHECK_RETURN(ret == NLSTK_ERRCODE_SUCCESS, "DT stack destroy channel failed");
}

void SleDataTransferService::NotifyDisconnect(const std::string &address)
{
    if (IsDeviceConnectingOrConnected(address)) {
        return;
    }
    PortService *portService = PortService::GetPortService();
    NL_CHECK_RETURN(portService, "PortService empty");
    RawAddress device(address);
    portService->Disconnect(device);
    HILOGI("notify disconnect success, addr: %{public}s", GetEncryptAddr(address).c_str());
}

bool SleDataTransferService::IsDeviceConnectingOrConnected(const std::string &address) const
{
    auto item = pimpl->appConnectInfo_.find(address);
    if (item != pimpl->appConnectInfo_.end()) {
        const auto &reqMap = item->second->appConnectReqMap_;
        if (!reqMap.empty()) {
            HILOGI("device %{public}s has %{public}zu pending connect requests",
                   GetEncryptAddr(address).c_str(), reqMap.size());
            return true;
        }
    }

    auto hasConnectReq = [&address](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> bool {
        return value->HasRemoteAddressConnect(address);
    };
    return pimpl->appConnectParamMap_.Find(hasConnectReq);
}

void SleDataTransferService::DestroyPortInner(const std::string &uuid, uint16_t port)
{
    HILOGI("port: %{public}d", port);
    NL_CHECK_RETURN(!uuid.empty(), "uuid empty when DestroyPort");
    std::vector<AppConnectParamMapping> tempVec;
    auto getConnects = [&tempVec](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> void {
        for (const auto &val : value->GetPortConnects()) {
            if (val.state == static_cast<int>(SleConnectState::CONNECTED) ||
                val.state == static_cast<int>(SleConnectState::CONNECTING)) {
                tempVec.push_back(val);
            }
        }
    };
    bool has = pimpl->appConnectParamMap_.GetValueAndOpt(port, getConnects);
    NL_CHECK_RETURN(has, "no cache when DestroyPort");
    if (uuid != STANDARD_UUID_ICCE) {
        ClearConnectReqByPort(port);
        // 上层异常退出触发死亡监听，未及时调用 Disconnect
        HILOGI("connected device number:%{public}zu", tempVec.size());
        for (const auto& item : tempVec) {
            HILOGI("release channel address: %{public}s tcid: %{public}u",
                GetEncryptAddr(item.address).c_str(), item.tcid);
            ReleasePortChannel(item.address, item.tcid);
        }

        PortService *portService = PortService::GetPortService();
        NL_CHECK_RETURN(portService, "PortService empty");
        portService->DeletePortByUuid((Uuid::ConvertFromString(uuid)).Uuid::ConvertTo128Bits(), port);
    }
    ports_.erase(port);
    pimpl->DeleteAppConnectParamMapping(port);
    HILOGI("current active port count: %{public}zu", ports_.size());
    DftReportDtfrStatisPortInfo(NearLinkPermissionManager::GetCallingName(), DTFR_DESTROY_PORT);
}

void SleDataTransferService::ClearConnectReqByPort(uint16_t port)
{
    HILOGI("enter");
    // delete connect req when remote app die
    std::vector<std::string> delVec;
    for (auto &v : pimpl->appConnectInfo_) {
        std::string key = v.first;
        std::unordered_map<uint16_t, DataTransferConnectionParams> &value = v.second->appConnectReqMap_;
        if (value.count(port)) {
            HILOGI("delete connect req: port=%{public}d - address=%{public}s", port, GetEncryptAddr(key).c_str());
            value.erase(port);
        }
        if (value.empty()) {
            delVec.push_back(key);
        }
    }
    for (const std::string &addr : delVec) {
        HILOGI("delete timer: address=%{public}s", GetEncryptAddr(addr).c_str());
        pimpl->ClearConnectReqAndTimer(addr);
    }
}

void SleDataTransferService::ClearCacheByTokenId(uint64_t tokenId)
{
    std::vector<DataTransferConnectionParams> tempVec;
    auto getCache = [&tempVec, tokenId](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> void {
        if (value->GetTokenId() == tokenId) {
            HILOGI("clear cache port: %{public}d", key);
            DataTransferConnectionParams temp;
            temp.SetPort(key);
            temp.SetUuid(value->GetUuid());
            tempVec.push_back(temp);
            // 车钥匙场景需要断acb
            for (const auto &val : value->GetPortConnects()) {
                SleDataTransferService::GetInstance().DisconnectCarWhenRemoteDie(val.address, value->GetUuid());
            }
        }
    };
    pimpl->appConnectParamMap_.Iterate(getCache);
    for (const auto& item : tempVec) {
        SleDataTransferService::GetInstance().DestroyPort(item.GetUuid(), item.GetPort());
        HILOGI("delete socket portId : %{public}d", item.GetPort());
    }
}

void SleDataTransferService::GetRemotePortByConnectionState(const std::string &addr, int state, int oldState)
{
    if (state == static_cast<int>(SleConnectState::DISCONNECTED)) {
        if (oldState == static_cast<int>(SleConnectState::CONNECTING)) { // profile超时连接
            HILOGI("portporfile conenct timeout");
            auto item = pimpl->appConnectInfo_.find(addr);
            if (item == pimpl->appConnectInfo_.end()) {
                HILOGE("not connect req");
                return;
            }
            std::unordered_map<uint16_t, DataTransferConnectionParams> &paramMap = item->second->appConnectReqMap_;
            for (const auto &v : paramMap) {
                DataTransferConnectionParams params = v.second;
                ReportDisconnectState(params);
            }
            pimpl->ClearConnectReqAndTimer(addr);
        } else if (oldState == static_cast<int>(SleConnectState::CONNECTED)) {
            HILOGD("normal disconnect response, ignore");
        } else {
            HILOGE("invalid state changed");
        }
    } else if (state == static_cast<int>(SleConnectState::CONNECTED)) {
        std::vector<DataTransferConnectionParams> tempVec;
        auto item = pimpl->appConnectInfo_.find(addr);
        if (item != pimpl->appConnectInfo_.end()) {
            HILOGD("enter");
            for (const auto &val : item->second->appConnectReqMap_) {
                HILOGD("after connect");
                tempVec.push_back(val.second);
            }
        } else {
            HILOGD("no connect request.");
        }
        pimpl->ClearConnectReqAndTimer(addr);
        for (const auto &param : tempVec) {
            GetRemotePortCreateChannel(param);
        }
    }
}

bool SleDataTransferService::IsProxyConnectExisted(std::string &devAddress)
{
    bool res = false;
    std::promise<std::pair<bool, std::string>> promise;
    std::future<std::pair<bool, std::string>> future = promise.get_future();
    DoInDataTransferThread([this, &promise]() {
        std::string tempAddress;
        auto stopNlProxy = [&tempAddress](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> bool {
            VerificationContext ctx = { .uuid = value->GetUuid() };
            bool result = NearlinkVerificationManager::GetInstance().CheckVerification(
                VerificationType::DATATRANSFER_PROXY, ctx);
            bool isAppConnected = value->IsAppConnect(tempAddress);
            if (result && isAppConnected) {
                return true;
            }
            return false;
        };
        bool isProxyConnect = pimpl->appConnectParamMap_.Find(stopNlProxy);
        promise.set_value(std::make_pair(isProxyConnect, tempAddress));
    });

    auto result = future.get();
    res = result.first;
    devAddress = result.second;
    return res;
}

void SleDataTransferService::StopNlProxyIfExisted()
{
    DoInDataTransferThread([this]() {
        auto stopNlProxy = [](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> bool {
            VerificationContext ctx = { .uuid = value->GetUuid() };
            bool result = NearlinkVerificationManager::GetInstance().CheckVerification(
                VerificationType::DATATRANSFER_PROXY, ctx);
            std::string address;
            bool isConnected = value->IsAppConnect(address);
            if (result && isConnected) {
                SleControllerService::GetInstance().UpdateConnectInterval(address, LOW_SPEED_INTERVAL_500);
                return true;
            }
            return false;
        };
        pimpl->appConnectParamMap_.Find(stopNlProxy);
    });
}

void SleDataTransferService::ChangeSocketState(uint16_t portId, std::string address, uint8_t result)
{
    DoInDataTransferThread([this, portId, address, result]() {
        ChangeSocketStateInner(portId, address, result);
    });
}

void SleDataTransferService::ChangeSocketStateInner(uint16_t portId, std::string address, uint8_t result)
{
    HILOGI("portId: %{public}d, address: %{public}s, result: %{public}u",
        portId, GetEncryptAddr(address).c_str(), result);
    std::vector<AppConnectParamMapping> tempVec;
    auto getConnects = [&address, &tempVec](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> void {
        for (const auto &val : value->GetPortConnects()) {
            if (val.address == address) {
                tempVec.push_back(val);
            }
        }
    };
    bool has = pimpl->appConnectParamMap_.GetValueAndOpt(portId, getConnects);
    NL_CHECK_RETURN(has, "portId invalid");
    for (const auto& item : tempVec) {
        SLE_Addr_S dev_addr;
        RawAddress addr(item.address);
        addr.ConvertToUint8(dev_addr.addr);
        TRANS_ChannelSetStatus(&dev_addr, item.tcid, result);
    }
}

void SleDataTransferService::ConnectCarInner(const DataTransferConnectionParams &params)
{
    auto sleInterfaceMgrPtr = SleInterfaceManager::GetInstance()->GetAdapter(SleTransport::ADAPTER_SLE);
    NL_CHECK_RETURN(sleInterfaceMgrPtr, "sleService is null.");
    std::string addr = params.GetAddress();
    RawAddress device(addr);
    if (sleInterfaceMgrPtr->IsAcbConnected(device)) {
        HILOGI("noneed connect acb");
        DataTransferConnectionParams dataTransferConnectionParams(params);
        ConnectCarChannelAfterAcb(dataTransferConnectionParams);
    } else {
        HILOGI("connect acb");
        AppConnectParamMapping temp;
        temp.address = addr;
        temp.state = static_cast<int32_t>(SleConnectState::CONNECTING);
        bool has = pimpl->UpdateAppConnectParamMapping(params.GetPort(), temp);
        NL_CHECK_RETURN(has, "no AppConnectParamMapping when connect car acb");
        sleInterfaceMgrPtr->ConnectAllProfile(device);
    }
}

void SleDataTransferService::ConnectCarChannelAfterAcb(DataTransferConnectionParams &params)
{
    // 1.获取对端port
    IcceService *icceService = IcceService::GetService();
    NL_CHECK_RETURN(icceService, "not support icce");
    int res = icceService->GetPort(RawAddress(params.GetAddress()));
    HILOGI("icceClient getRemotePort: %{public}d", res);
    uint16_t remotePort = res < 0 ? CAR_PORT : static_cast<uint16_t>(res);
    if (res < 0) {
        DftReportDtfrExcepInfo(params.GetAddress(), params.GetUuid(), EXCEP_CONNECT, GET_ICCE_PORT_FAIL);
    }
    // 2.车钥匙数传通道协议栈默认已创建, 构造已连接回调事件
    AppConnectParamMapping temp;
    temp.address = params.GetAddress();
    temp.dstPort = remotePort;
    temp.tcid = TCID_SLE_DEFAULT;
    temp.state = static_cast<int32_t>(SleConnectState::CONNECTED);
    HILOGI("ConnectCarChannelAfterAcb port=%{public}d", params.GetPort());
    HandleConnectEvent(QOSM_TRANS_CHANNEL_ESTABLISHED, params.GetPort(), DEFAULT_CAR_MTU, temp);
}

void SleDataTransferService::ConnectPeerPortInner(const DataTransferConnectionParams &params)
{
    VerificationContext ctx = { .uuid = params.GetUuid() };
    bool result = NearlinkVerificationManager::GetInstance().CheckVerification(
        VerificationType::DATATRANSFER_PROXY, ctx);
    if (result && pimpl->CheckUuidIsConnect(STANDARD_UUID_ICCE)) {
        HILOGI("hasCarKeyConnect");
        DataTransferConnectionParams dataTransferConnectionParams(params);
        dataTransferConnectionParams.SetState(static_cast<int32_t>(SleConnectState::DISCONNECTED));
        NL_CHECK_RETURN(pimpl->callback_, "callback_ null");
        pimpl->callback_->OnConnectionStateChanged(dataTransferConnectionParams, INVALID_FD);
        return;
    }

    RawAddress device(params.GetAddress());
    PortService *portService = PortService::GetPortService();
    NL_CHECK_RETURN(portService, "ConnectPeerPortInner PortService empty");
    int state = portService->GetConnectState(device);
    HILOGD("ConnectPeerPortInner srcPort: %{public}u, addr: %{public}s, curState: %{public}d", params.GetPort(),
        GET_ENCRYPT_ADDR(device), state);
    if (state != static_cast<int>(SleConnectState::CONNECTED)) {
        ConnectAction(device, params);
        return;
    }

    bool hasParam = false;
    DataTransferConnectionParams temp(params);
    temp.state_ = static_cast<int32_t>(SleConnectState::DISCONNECTED);
    auto getConnects = [&temp, &hasParam, addr = params.GetAddress()](uint16_t key,
        std::shared_ptr<SleDataTransferCache> value) -> void {
        AppConnectParamMapping param;
        hasParam = value->GetAppConnectParamByAddr(addr, param);
        if (hasParam) {
            temp.state_ = param.state;
            temp.transMode_ = param.transState;
            temp.frameType_ = param.frameType;
        }
    };
    bool has = pimpl->appConnectParamMap_.GetValueAndOpt(params.GetPort(), getConnects);
    NL_CHECK_RETURN(has, "can not find appConnectParam");
    if (hasParam && (temp.state_ == static_cast<int32_t>(SleConnectState::CONNECTED) ||
        temp.state_ == static_cast<int32_t>(SleConnectState::CONNECTING))) { // 已执行过 connectAction
        HILOGI("portId: %{public}d, addr: %{public}s, connectParam state: %{public}d",
            params.GetPort(), GET_ENCRYPT_ADDR(device), temp.state_);
        NL_CHECK_RETURN(pimpl->callback_, "callback_ null");
        pimpl->callback_->OnConnectionStateChanged(temp, INVALID_FD);
    } else { // 若ACB和PORT PROFILE已连接 直接创建PORT CHANNEL
        GetRemotePortCreateChannel(params);
    }
}

bool SleDataTransferService::TryUpdateConnectReqParam(const DataTransferConnectionParams &params)
{
    AppConnectParamMapping existingParam;
    bool hasExisting = pimpl->GetAppConnectParamMapping(params.GetPort(), params.GetAddress(), existingParam);
    if (hasExisting && existingParam.state == static_cast<int32_t>(SleConnectState::CONNECTED)) {
        HILOGI("port already connected, skip update, portId: %{public}d, addr: %{public}s, state: %{public}d",
            params.GetPort(), GetEncryptAddr(params.GetAddress()).c_str(), existingParam.state);
        return false;
    }
    AppConnectParamMapping temp;
    temp.address = params.GetAddress();
    temp.state = static_cast<int32_t>(SleConnectState::CONNECTING);
    temp.transMode = params.GetTransMode();
    temp.frameType = params.GetFrameType();
    return pimpl->UpdateAppConnectParamMapping(params.GetPort(), temp);
}

void SleDataTransferService::ConnectAction(const RawAddress &device, const DataTransferConnectionParams &params)
{
    PortService *portService = PortService::GetPortService();
    NL_CHECK_RETURN(portService, "ConnectAction PortService empty");
    std::string remoteAddr = params.GetAddress();
    int srcPort = params.GetPort();
    NL_CHECK_RETURN(TryUpdateConnectReqParam(params), "tryUpdateConnectReqParam failed");
    auto sleAdapter =
        static_cast<SleInterfaceAdapterSub *>(SleInterfaceManager::GetInstance()->GetAdapter(ADAPTER_SLE));
    NL_CHECK_RETURN(sleAdapter, "sleAdapter null");
    sleAdapter->SetConnFrameType(device.GetAddress(), static_cast<uint8_t>(params.frameType_));

    // try insert
    bool hasCache = false;
    std::shared_ptr<impl::PortConnectInfo> info = std::make_shared<impl::PortConnectInfo>();
    if (pimpl->appConnectInfo_.find(remoteAddr) == pimpl->appConnectInfo_.end()) {
        pimpl->appConnectInfo_[remoteAddr] = info;
    } else {
        info = pimpl->appConnectInfo_[remoteAddr];
    }
    std::unordered_map<uint16_t, DataTransferConnectionParams> &connReq = info->appConnectReqMap_;
    if (!connReq.count(srcPort)) {
        HILOGI("add connect request, addr: %{public}s, portId: %{public}d",
            GetEncryptAddr(remoteAddr).c_str(), srcPort);
        connReq[srcPort] = params;
    } else {
        hasCache = true;
    }
    // 没有连接请求缓存, 则发起ACB连接, 并且缓存连接请求, 起定时器超时删除连接缓存.
    NL_CHECK_RETURN(!hasCache, "ignore repeat connect");

    portService->ConnectWithParam(device, PortServiceConnParam(params.GetFrameType()));
    auto timeoutFunc = [this, addr = remoteAddr, portId = srcPort]() -> void {
        HILOGI("portService connectable timeout");
        DoInDataTransferThread([this, addr, portId]() -> void {
            ConnectTimeout(addr, portId);
        });
    };
    if (info->appConnectTimer_ != nullptr) {
        info->appConnectTimer_->Stop();
    }
    info->appConnectTimer_ = std::make_shared<NearlinkTimer>(timeoutFunc);
    int32_t time = PORT_CONN_WAIT_TIMEOUT;  // Timer 11s  clearCache
    info->appConnectTimer_->Start(time, false);
}

void SleDataTransferService::CreateSocketTransferChannel(uint16_t port, const std::string &address, uint16_t mtu,
    int &fd)
{
    NL_CHECK_RETURN(port, "port is invalid.");
    // socketpair 创建本端和对端socket, 添加工作线程需执行的数据读取函数
    fd = pimpl->portSocketManager_->CreateSocketPair(port, address, mtu, [this](
        std::shared_ptr<InputStream> inputStream, uint16_t portId, std::string address) {
        bool ret = ReceivedData(inputStream, portId, address);
        if (ret) { // success
            return;
        }
    });
    pimpl->portSocketManager_->Listen(port, address);
    return;
}

void SleDataTransferService::ConnectTimeout(const std::string addr, const uint16_t portId)
{
    HILOGI("connect timeout addr: %{public}s, portId: %{public}d", GetEncryptAddr(addr).c_str(), portId);
    // clear connect cache
    auto item = pimpl->appConnectInfo_.find(addr);
    NL_CHECK_RETURN(item != pimpl->appConnectInfo_.end(), "no appConnectInfo.");
    std::unordered_map<uint16_t, DataTransferConnectionParams> &connParam = item->second->appConnectReqMap_;
    DataTransferConnectionParams params = connParam[portId];
    connParam.erase(portId);
    if (connParam.empty()) {
        pimpl->ClearConnectReqAndTimer(addr);
    }

    // change connect state
    ReportDisconnectState(params);
}

void SleDataTransferService::ReportDisconnectState(const DataTransferConnectionParams &params)
{
    AppConnectParamMapping temp;
    DataTransferConnectionParams reportParam(params);
    bool has = pimpl->GetAppConnectParamMapping(params.GetPort(), params.GetAddress(), temp);

    NL_CHECK_RETURN(has, "can not find appConnectParam");
    if (temp.state == static_cast<int32_t>(SleConnectState::CONNECTING)) {
        temp.state = static_cast<int32_t>(SleConnectState::DISCONNECTED);
        reportParam.SetState(static_cast<int32_t>(SleConnectState::DISCONNECTED));
        pimpl->UpdateAppConnectParamMapping(params.GetPort(), temp);
        HILOGI("set appConnectParam disconnect.");
        NL_CHECK_RETURN(pimpl->callback_, "callback_ null");
        pimpl->callback_->OnConnectionStateChanged(reportParam, INVALID_FD); // 上报状态更新为断链信息
    }
}

void SleDataTransferService::GetRemotePortCreateChannel(const DataTransferConnectionParams &params)
{
    NL_CHECK_RETURN(TryUpdateConnectReqParam(params), "tryUpdateConnectReqParam failed");
    // 1.获取对端port
    PortService *portService = PortService::GetPortService();
    NL_CHECK_RETURN(portService, "PortService empty");
    uint16_t remotePort = portService->GetRemotePortByUuid(RawAddress(params.GetAddress()),
        (Uuid::ConvertFromString(params.GetUuid())).Uuid::ConvertTo128Bits());
    if (remotePort == 0) {
        DftReportDtfrExcepInfo(params.GetAddress(), params.GetUuid(), EXCEP_CONNECT, GET_PEER_PORT_FAIL);
        HILOGE("dt getRemotePort failed");
        ReportDisconnectState(params);
        return;
    }

    // 2.获取对端tcid
    QOSM_TransChannelParams_S channelParams;
    (void)memset_s(&channelParams, sizeof(channelParams), 0x00, sizeof(channelParams));
    RawAddress addr(params.GetAddress());
    addr.ConvertToUint8(channelParams.addr.addr);
    channelParams.linkMode = SLE_MODE_ACB;
    channelParams.accessTransMode = ACCESS_TRANS_MODE_UNICAST;
    channelParams.srcPort = params.port_;
    channelParams.dstPort = remotePort;
    channelParams.slqi = QOSM_TRANS_CHANNEL_SLQI_HIGH;
    channelParams.frameType = static_cast<QOSM_TransConnFrameType_E>(params.frameType_);
    if (SleRemoteDeviceAdapter::GetInstance()->IsAudioDevice(params.GetAddress())) {
        // 音频业务在建数传的链路时候，不调整底层参数，避免影响同步链路建立
        channelParams.slqi = QOSM_TRANS_CHANNEL_SLQI_LOW;
    }
    channelParams.tcConf.mode = params.GetTransMode();
    HILOGI("channelParams.tcConf.mode %{public}u, channelParams.frameType %{public}u",
        channelParams.tcConf.mode, channelParams.frameType);
    NLSTK_ERRCODE ret = QOSM_TransChannelCreate(&channelParams);
    if (ret != NLSTK_ERRCODE_SUCCESS) {
        DftReportDtfrExcepInfo(params.address_, params.GetUuid(), EXCEP_CONNECT, CONNECT_GET_TCID_ERR);
        HILOGE("DT stack create channel failed");
        ReportDisconnectState(params);
        return;
    }
}

void SleDataTransferService::Connect(DataTransferConnectionParams &params) const
{
    HILOGD("enter");
    DftDtfrRecordCallName(params.GetAddress(), NearLinkPermissionManager::GetCallingName());
    DoInDataTransferThread([params]() {
        if (params.GetUuid() == STANDARD_UUID_ICCE) {
            SleDataTransferService::GetInstance().ConnectCarInner(params);
        } else {
            SleDataTransferService::GetInstance().ConnectPeerPortInner(params);
        }
    });
}

void SleDataTransferService::Disconnect(DataTransferConnectionParams &params) const
{
    DoInDataTransferThread([this, params]() {
        DataTransferConnectionParams dataTransferConnectionParams(params);
        DisconnectInner(dataTransferConnectionParams);
    });
}

void SleDataTransferService::DisconnectInner(DataTransferConnectionParams &params) const
{
    HILOGD("enter");
    std::string address = params.GetAddress();
    DftDtfrRecordCallName(address, NearLinkPermissionManager::GetCallingName());
    if (params.GetUuid() == STANDARD_UUID_ICCE) {
        NL_CHECK_RETURN(pimpl->callback_, "callback_ null");
        auto updateState = [&address](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> void {
            value->UpdateStateByAddr(address, static_cast<int32_t>(SleConnectState::DISCONNECTED));
        };
        bool has = pimpl->appConnectParamMap_.GetValueAndOpt(params.GetPort(), updateState);
        NL_CHECK_RETURN(has, "no AppConnectParamMapping when icce Disconnect");
        params.SetState(static_cast<int32_t>(SleConnectState::DISCONNECTED));
        pimpl->callback_->OnConnectionStateChanged(params, INVALID_FD);
        DftReportDtfrStatisInfo(QOSM_TRANS_CHANNEL_RELEASED, address, params.GetUuid());
    } else {
        uint8_t tcid = 0;
        auto updateState = [&address, &tcid](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> void {
            tcid = value->UpdateStateAndGetTcidByAddr(address, static_cast<int32_t>(SleConnectState::DISCONNECTING));
        };
        bool has = pimpl->appConnectParamMap_.GetValueAndOpt(params.GetPort(), updateState);
        NL_CHECK_RETURN(has, "no AppConnectParamMapping when port Disconnect");
        NL_CHECK_RETURN(tcid > 0, "tcid invalid");
        ReleasePortChannel(address, tcid);
    }
}

int32_t SleDataTransferService::GetConnectionState(DataTransferConnectionParams &params) const
{
    std::string addr = params.GetAddress();
    uint16_t srcPort = params.GetPort();
    std::promise<uint16_t> promise;
    DoInDataTransferThread([this, srcPort, addr, &promise]() {
        AppConnectParamMapping temp;
        temp.state = static_cast<int32_t>(SleConnectState::CONNECTING);
        bool has = pimpl->GetAppConnectParamMapping(srcPort, addr, temp);
        if (!has) {
            HILOGE("can not find appConnectParam");
        }
        HILOGI("GetConnectionState state=%{public}d", temp.state);
        promise.set_value(temp.state);
    });
    int32_t res = promise.get_future().get();
    HILOGI("temp Port=%{public}d state=%{public}d", srcPort, res);
    return res;
}

int SleDataTransferService::GetConnectionTransferState(uint16_t portId, const std::string &address, int &transState,
    int &preTransState) const
{
    AppConnectParamMapping temp;
    bool hasParam = false;
    auto getAppConnectParam = [&temp, &hasParam, &address, &transState, &preTransState](uint16_t key,
        std::shared_ptr<SleDataTransferCache> value) -> void {
        hasParam = value->GetAppConnectParamByAddr(address, temp);
        NL_CHECK_RETURN(hasParam, "no connect param when GetConnectionState");
        transState = temp.transState;
        preTransState = temp.preTransState;

        if (temp.preTransState != temp.transState) {
            HILOGI("temp Port=%{public}d transState=%{public}d", key, temp.transState);
            temp.preTransState = temp.transState;
            value->SetPortConnects(temp);
        }
    };
    bool has = pimpl->appConnectParamMap_.GetValueAndOpt(portId, getAppConnectParam);
    NL_CHECK_RETURN_RET(hasParam && has, TransferReturnValue::SLE_TRANS_INNER_FAIL, "no cache when GetConnectionState");
    return 0;
}

int SleDataTransferService::WriteData(DataTransferDataParams &params) const
{
    HILOGD("enter");
    AppConnectParamMapping temp;
    bool has = pimpl->GetAppConnectParamMapping(params.port_, params.address_, temp);
    NL_CHECK_RETURN_RET(has, NLSTK_ERRCODE_PARAM_ERR, "no AppConnectParamMapping");
    HILOGD("temp dstPort=%{public}d tcid=%{public}d", temp.dstPort, temp.tcid);
    if (temp.tcid == 0) {
        DftDtfrRecordCallName(params.address_, NearLinkPermissionManager::GetCallingName());
        DftReportDtfrExcepInfo(params.address_, params.uuid_, EXCEP_WRITE, WRITE_PORT_CHANNEL_ERR);
        HILOGE("tcid invalid when writeData");
        return NLSTK_ERRCODE_PARAM_ERR;
    }
    TRANS_Addr_S connectionParams;
    (void)memset_s(&connectionParams, sizeof(connectionParams), 0x00, sizeof(connectionParams));
    RawAddress addr(params.address_);
    addr.ConvertToUint8(connectionParams.devAddr.addr);
    connectionParams.tcid = temp.tcid;
    connectionParams.proto = TRANS_PROTO_CONNECTIONLESS;
    connectionParams.srcPort = params.port_;
    connectionParams.dstPort = temp.dstPort;
    uint32_t ret = TRANS_SendData(&connectionParams, params.data_.data(), params.data_.size());
    if (ret == TRANS_RESULT_TX_CACHE_FULL) {
        temp.preTransState = temp.transState;
        temp.transState = TransferState::TRANSFER_BUSY;
        pimpl->UpdateAppConnectParamMapping(params.port_, temp);
        HILOGI("temp srcPort=%{public}d transState=%{public}d, preTransState=%{public}d",
               params.port_, temp.transState, temp.preTransState);
        return TRANS_BUSY;
    }
    if (ret != TRANS_RESULT_SUCCESS && ret != TRANS_RESULT_TX_CACHE_FULL) {
        DftDtfrRecordCallName(params.address_, NearLinkPermissionManager::GetCallingName());
        DftReportDtfrExcepInfo(params.address_, params.uuid_, EXCEP_WRITE, WRITE_PORT_CHANNEL_ERR);
        HILOGE("DT stack send data failed");
        return TRANS_FAIL;
    }

    return TRANS_SUCCESS;
}

int SleDataTransferService::ReceiveDataCallback(const TRANS_Addr_S *addr, uint8_t *data, uint16_t len)
{
    NL_CHECK_RETURN_RET(addr, TransferReturnValue::SLE_TRANS_INNER_FAIL, "stack cb addr null");
    NL_CHECK_RETURN_RET(len > 0, TransferReturnValue::SLE_TRANS_INNER_FAIL, "stack cb data len invalid");
    RawAddress rawAddress(RawAddress::ConvertToString(addr->devAddr.addr));
    std::string address = rawAddress.GetAddress();
    std::vector<uint8_t> datas(data, data + len);
    int ret = SleDataTransferService::GetInstance().HandleReceiveDataEvent(address, addr->dstPort, datas);
    if (ret != SocketTransState::SLE_TRANS_RESULT_SUCCESS) {
        HILOGW("ReceiveDataCallback %{public}d", ret);
    }
    return ret;
}

void SleDataTransferService::SendDataStateCallback(const SLE_Addr_S *devAddr, uint8_t tcid, uint16_t portId,
    uint8_t result)
{
    NL_CHECK_RETURN(devAddr != nullptr, "SendDataStateCallback devAddr is null");
    RawAddress rawAddress(RawAddress::ConvertToString(devAddr->addr));
    std::string address = rawAddress.GetAddress();

    HILOGI("SendDataStateCallback enter");
    DoInDataTransferThread([address, tcid, portId, result]() {
        SleDataTransferService::GetInstance().UpdateTransferState(address, tcid, portId, result);
        HILOGI("SendDataStateCallback address: %{public}s , ticd: %{public}u portId: %{public}d ,result: %{public}d",
            GetEncryptAddr(address).c_str(), tcid, portId, result);
    });
}

bool SleDataTransferService::CheckIpSharePeerCallback(const SLE_Addr_S *addr, uint16_t srcPort)
{
    return NearlinkIpShareChannel::IsAcceptingPort(addr, srcPort);
}

bool SleDataTransferService::CheckChannelParamCallback(uint16_t srcPort)
{
    if (NearlinkIpShareChannel::IsIpSharePort(srcPort)) {
        HILOGI("[IpShare][DataTransfer] accepted IPoSL QoSM channel port=%{public}u", srcPort);
        return true;
    }
    return SleDataTransferService::GetInstance().IsValidSrcPort(srcPort);
}

bool SleDataTransferService::IsValidSrcPort(uint16_t srcPort)
{
    if (!pimpl->appConnectParamMap_.FindIf(srcPort)) {
        HILOGE("can no find src port.");
        return false;
    }
    return true;
}

void SleDataTransferService::ChannelStatusCallback(const QOSM_TransChannelRspParams_S *respParams)
{
    HILOGD("dt channel cb");
    NL_CHECK_RETURN(respParams, "stack dt channel cb null");
    if (NearlinkIpShareChannel::HandleChannelStatus(respParams)) {
        HILOGI("[IpShare][DataTransfer] dispatched IPoSL channel status=%{public}d lcid=%{public}u tcid=%{public}u",
            respParams->status, respParams->lcid, respParams->tcid);
        return;
    }
    RawAddress rawAddress(RawAddress::ConvertToString(respParams->addr.addr));
    QOSM_TransChannelStatus_E state = respParams->status;
    AppConnectParamMapping temp;
    temp.dstPort = respParams->dstPort;
    temp.tcid = respParams->tcid;
    temp.address = rawAddress.GetAddress();
    if (state == QOSM_TRANS_CHANNEL_ESTABLISHED) {
        temp.state = static_cast<int32_t>(SleConnectState::CONNECTED);
        HILOGI("connected, data mtu:%{public}d", respParams->mtu);
    } else if (state == QOSM_TRANS_CHANNEL_RELEASED || state == QOSM_TRANS_CHANNEL_ESTABLISH_FAIL) {
        HILOGI("channel destroy success");
        temp.state = static_cast<int32_t>(SleConnectState::DISCONNECTED);
    } else {
        HILOGI("channel ESTABLISH release FAIL");
        temp.state = static_cast<int32_t>(SleConnectState::CONNECTING);
    }
    SleDataTransferService::GetInstance().HandleConnectEvent(state, respParams->srcPort, respParams->mtu, temp);
}

void SleDataTransferService::UpdateTransferState(const std::string address, uint8_t tcid, uint16_t portId,
    uint8_t result)
{
    auto updateCache = [&address, tcid, result](uint16_t key, std::shared_ptr<SleDataTransferCache> value) -> void {
        value->UpdateTransferState(address, tcid, result);
    };
    bool has = pimpl->appConnectParamMap_.GetValueAndOpt(portId, updateCache);
    NL_CHECK_RETURN(has, "portId invalid");
    HILOGI("UpdateTransferState portId = %{public}d, transState = %{public}d", portId, result);
}

void SleDataTransferService::HandleConnectEvent(int32_t stat, uint16_t srcPort, uint16_t mtu,
    AppConnectParamMapping temp)
{
    std::string uuid = pimpl->FindUuidByPort(srcPort);
    NL_CHECK_RETURN(!uuid.empty(), "can not find uuid when HandleConnectEvent");

    // 避免创建两条数传通道, 上报两次状态
    AppConnectParamMapping oldParam;
    oldParam.state = static_cast<int32_t>(SleConnectState::INVALID_STATE);
    bool ret = pimpl->GetAppConnectParamMapping(srcPort, temp.address, oldParam);
    if (ret && oldParam.state == temp.state) {
        HILOGI("repeat connect event, ignore.");
        return;
    }

    int fd = INVALID_FD;
    if (temp.state == static_cast<int32_t>(SleConnectState::CONNECTED)) {
        CreateSocketTransferChannel(srcPort, temp.address, mtu, fd);
        pimpl->portSocketManager_->RunThread();
    } else if (temp.state == static_cast<int32_t>(SleConnectState::DISCONNECTED)) {
        pimpl->portSocketManager_->DestroyPeerPort(srcPort, temp.address);
    }

    DataTransferConnectionParams connectionParams;
    connectionParams.SetUuid(uuid);
    connectionParams.SetPort(srcPort);
    connectionParams.SetMtu(mtu);
    connectionParams.SetAddress(temp.address);
    connectionParams.SetState(temp.state);

    std::shared_ptr<SleDataTransferCache> cache;
    bool res = pimpl->appConnectParamMap_.GetValue(srcPort, cache);
    NL_CHECK_RETURN(res, "can not find app tokenId");
    uint64_t tokenId = cache->GetTokenId();
    NL_CHECK_RETURN(pimpl->callback_ != nullptr, "callback_ is nullptr");
    temp.randomAddress = pimpl->callback_->GetRandomAddr(temp.address, cache->GetTokenId());

    DoInDataTransferThread([this, connectionParams, temp, fd, cache]() {
        bool has = pimpl->UpdateAppConnectParamMapping(connectionParams.port_, temp);
#ifdef RES_SCHED_SUPPORT
        CheckReportNlConnectStateToRss(connectionParams, temp, cache);
#endif
        NL_CHECK_RETURN(has, "no AppConnectParamMapping when channel status cb");
        if (temp.state == static_cast<int32_t>(SleConnectState::DISCONNECTED)) {
            NotifyDisconnect(connectionParams.address_);
        }
        NL_CHECK_RETURN(pimpl->callback_, "callback_ null");
        pimpl->callback_->OnConnectionStateChanged(connectionParams, fd);
    });
    DftReportDtfrStatisInfo(stat, temp.address, uuid);
}

#ifdef RES_SCHED_SUPPORT
void SleDataTransferService::CheckReportNlConnectStateToRss(DataTransferConnectionParams connectionParams,
    AppConnectParamMapping temp, std::shared_ptr<SleDataTransferCache> cache)
{
    std::string action;
    int32_t pid = pimpl->FindPidByPort(connectionParams.port_);
    int32_t uid = pimpl->FindUidByPort(connectionParams.port_);
    if (temp.state == static_cast<int32_t>(SleConnectState::CONNECTED)) {
        action = "CONNECT";
        NearlinkFreezeUtil::GetInstance()->ReportNlConnectStateToRss(
            pid, uid, action, "SOCKET", "SOCKET " + action);
    } else if (temp.state == static_cast<int32_t>(SleConnectState::DISCONNECTED)) {
        bool disConnect = cache->needRssReportDisconnect();
        if (!disConnect){
            action = "ACTIVE_DISCONNECT";
            NearlinkFreezeUtil::GetInstance()->ReportNlConnectStateToRss(
                pid, uid, action, "SOCKET", "SOCKET " + action);
        }
    }
}
#endif

int SleDataTransferService::RegisterSleDataTransferCallbackToStack()
{
    HILOGD("enter");
    TRANS_Cbks_S transCallbacks;
    transCallbacks.recvDataCbk = &SleDataTransferService::ReceiveDataCallback;
    transCallbacks.sendDataCbk = &SleDataTransferService::SendDataStateCallback;
    uint32_t ret = TRANS_RegisterCbks(&transCallbacks);
    HILOGI("DT stack cb ret=0x%{public}x", ret);

    QOSM_TransChannelCbks_S transChannelCbks;
    transChannelCbks.statusCbk = &SleDataTransferService::ChannelStatusCallback;
    transChannelCbks.establishedCheck = &SleDataTransferService::CheckChannelParamCallback;
    ret = QOSM_TransChannelCbksRegister(&transChannelCbks);
    HILOGI("DT stack channel cb ret=0x%{public}x", ret);
    if (ret == 0) {
        ret = QOSM_RegisterIpSharePeerCheck(&SleDataTransferService::CheckIpSharePeerCallback);
    }
    return ret;
}

int SleDataTransferService::DeregisterCallback()
{
    TRANS_UnregisterCbks();
    QOSM_TransChannelCbksUnregister();
    return NLSTK_ERRCODE_SUCCESS;
}

#ifdef RES_SCHED_SUPPORT
void SleDataTransferService::CheckRssAppState(uint64_t tokenId, uint32_t uid)
{
    DoInDataTransferThread([this, tokenId, uid]() {
        if (pimpl->cancelRssTimer_ != nullptr) {
            return;
        }
        pimpl->NotifyRssAppStateChanged(tokenId, uid, false);

        auto timeoutFunc = [this, tokenId, uid]() -> void {
            HILOGI("rss app thaw timeout");
            DoInDataTransferThread([this, tokenId, uid]() -> void {
                pimpl->NotifyRssAppStateChanged(tokenId, uid, true);
                pimpl->cancelRssTimer_->Stop();
                pimpl->cancelRssTimer_ = nullptr;
            });
        };
        pimpl->cancelRssTimer_ = std::make_shared<NearlinkTimer>(timeoutFunc);
        pimpl->cancelRssTimer_->Start(RSS_THAW_TIMEOUT, false); // Timer 20s
    });
}
#endif

int SleDataTransferService::HandleReceiveDataEvent(const std::string &address, uint16_t dstPort,
    const std::vector<uint8_t> &datas)
{
    HILOGD("dstPort: %{public}d", dstPort);
    std::string randomAddr = address;
    int32_t pid = pimpl->FindPidByPort(dstPort);
    int32_t uid = pimpl->FindUidByPort(dstPort);
    // 车钥匙需要处理映射
    if (dstPort == CAR_PORT) {
        std::vector<uint16_t> receivePorts;
        auto findReceivePort = [this, &address, &receivePorts, &randomAddr](uint16_t key,
            std::shared_ptr<SleDataTransferCache> val) -> void {
            if (val->GetUuid() == STANDARD_UUID_ICCE && val->HasTargetAddr(address)) {
                receivePorts.push_back(key);
                randomAddr = val->GetRandomAddrByAddr(address);
#ifdef RES_SCHED_SUPPORT
                CheckRssAppState(val->GetTokenId(), val->GetUid());
#endif
            }
        };
        pimpl->appConnectParamMap_.Iterate(findReceivePort);
        NL_CHECK_RETURN_RET(!receivePorts.empty(), TransferReturnValue::SLE_TRANS_INNER_FAIL, "no car app register");
        for (const auto &port : receivePorts) {
            HILOGD("receivePort: %{public}d", port);
#ifdef RES_SCHED_SUPPORT
        NearlinkFreezeUtil::GetInstance()->RequestActive(pid, uid, "SOCKET", "SOCKETCARDataRecv");
#endif
            DistributeDataFromStack(STANDARD_UUID_ICCE, address, randomAddr, port, datas);
        }
        return SLE_TRANS_SUCCESS;
    }
    std::string uuid = pimpl->FindUuidByPort(dstPort);
    std::shared_ptr<SleDataTransferCache> cache;
    bool res = pimpl->appConnectParamMap_.GetValue(dstPort, cache);
    NL_CHECK_RETURN_RET(res, SLE_TRANS_RESULT_INTERNAL_FAULT, "can not find tokenId");
    randomAddr = cache->GetRandomAddrByAddr(address);
    NL_CHECK_RETURN_RET(!uuid.empty(), TransferReturnValue::SLE_TRANS_INNER_FAIL,
        "can not find uuid when HandleReceiveDataEvent");
#ifdef RES_SCHED_SUPPORT
    NearlinkFreezeUtil::GetInstance()->RequestActive(pid, uid, "SOCKET", "SOCKETNormalDataRecv");
#endif
    return DistributeDataFromStack(uuid, address, randomAddr, dstPort, datas);
}

int SleDataTransferService::DistributeDataFromStack(const std::string &uuid, const std::string &address,
    const std::string &randomAddr, uint16_t dstPort, const std::vector<uint8_t> &datas)
{
    DataTransferDataParams dataParams(randomAddr, uuid, dstPort, datas);

    // send data to framework by socket
    size_t totalLen = 0;
    std::unique_ptr<uint8_t[]> packageData = NearlinkDataTransferDataParams::SerializeData(dataParams, totalLen);
    NL_CHECK_RETURN_RET(packageData != nullptr, SocketTransState::SLE_TRANS_RESULT_INTERNAL_FAULT,
        "SerializeData err");
    HILOGD("packageData len : %{public}zu", totalLen);
    SocketTransState ret = pimpl->portSocketManager_->SendData(dataParams.port_, address, packageData.get(), totalLen,
        datas.size());
    if (ret == SocketTransState::SLE_TRANS_RESULT_CACHE_FULL) {
        HILOGW("socket cache list is full");
        return SLE_TRANS_RESULT_CACHE_FULL;
    } else if (ret == SocketTransState::SLE_TRANS_RESULT_SUCCESS) {
        return SLE_TRANS_RESULT_SUCCESS;
    }
    HILOGD("SendData ret=%{public}d", ret);
    return ret;
}

void SleDataTransferService::DisconnectCarWhenRemoteDie(const std::string &address, const std::string &uuid)
{
    HILOGD("enter");
    auto sleService = SleInterfaceManager::GetInstance()->GetAdapter(SleTransport::ADAPTER_SLE);
    NL_CHECK_RETURN(sleService, "sleService null");
    if (uuid == STANDARD_UUID_ICCE && !address.empty()) {
        HILOGI("disconnect car");
        RawAddress addr(address);
        sleService->DisconnectAction(addr, static_cast<uint8_t>(SleDiscReason::SLE_DISC_REASON_REMOTE_USER_TERMINATED));
    }
}
}  // namespace OHOS::Nearlink
