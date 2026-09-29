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

#ifndef OHOS_NEARLINK_DATATRANSFER_SERVICE_H
#define OHOS_NEARLINK_DATATRANSFER_SERVICE_H

#include "nearlink_types.h"
#include "SleInterfaceDataTransfer.h"
#include "nearlink_sle_datatransfer_cache.h"
#include "transport_internal.h"
#include "qosm_trans_channel.h"
#include <set>
#include "BaseDef.h"
#include "nearlink_socket_inputstream.h"
#include "ffrt_inner.h"

namespace OHOS::Nearlink {

enum TransferReturnValue {
    SLE_TRANS_INNER_FAIL = -1, // 内部错误
    SLE_TRANS_SUCCESS = 0,     // 成功
};

/**
 * @brief SLE datatransfer service.
 */
class SleDataTransferService : public SleInterfaceDataTransfer {
public:
    static SleDataTransferService &GetInstance();

    SleDataTransferService();
    ~SleDataTransferService();

    void RegisterSleDataTransferServiceCallback(std::shared_ptr<ISleDataTransferServiceCallback> callback) override;
    void DeregisterSleDataTransferServiceCallback() override;
    uint16_t CreatePort(const std::string &uuid) override;
    void DestroyPort(const std::string &uuid, uint16_t port) override;
    void Connect(DataTransferConnectionParams &params) const override;
    void Disconnect(DataTransferConnectionParams &params) const override;
    int32_t GetConnectionState(DataTransferConnectionParams &params) const override;
    int GetConnectionTransferState(uint16_t portId, const std::string &address, int &transState,
        int &preTransState) const override;
    void ChangeSocketState(uint16_t portId, std::string address, uint8_t result) override;
    int WriteData(DataTransferDataParams &params) const override;
    void ClearCacheByTokenId(uint64_t tokenId) override;
    void GetRemotePortByConnectionState(const std::string &addr, int state, int oldState);
    void StopNlProxyIfExisted();
    bool IsValidSrcPort(uint16_t srcPort);
    bool IsProxyConnectExisted(std::string &devAddress) override;

private:
    int RegisterSleDataTransferCallbackToStack();
    int DeregisterCallback();
    uint16_t CreatePortInner(const std::string &uuid, const uint64_t tokenId, const uint32_t uid, const uint32_t pid);
    void ConnectTimeout(const std::string addr, const uint16_t portId);
    void DestroyPortInner(const std::string &uuid, uint16_t port);
    void ClearConnectReqByPort(uint16_t port);
    void DisconnectInner(DataTransferConnectionParams &params) const;
    void ConnectCarInner(const DataTransferConnectionParams &params);
    void ConnectCarChannelAfterAcb(DataTransferConnectionParams &params);
    void DisconnectCarWhenRemoteDie(const std::string &address, const std::string &uuid);
    void ConnectPeerPortInner(const DataTransferConnectionParams &params);
    void ConnectAction(const RawAddress &device, const DataTransferConnectionParams &params);
    bool TryUpdateConnectReqParam(const DataTransferConnectionParams &params);
    void GetRemotePortCreateChannel(const DataTransferConnectionParams &params);
    void ReportDisconnectState(const DataTransferConnectionParams &params);
    void UpdateTransferState(const std::string address, uint8_t tcid, uint16_t portId, uint8_t result);
    bool ReceivedData(std::shared_ptr<InputStream> inputStream, uint16_t portId, const std::string &address);
    int GetTransferState(uint16_t portId,const std::string &address);
    void ChangeSocketStateInner(uint16_t portId, std::string address, uint8_t result);
    int HandleReceiveDataEvent(const std::string &address, uint16_t dstPort, const std::vector<uint8_t> &datas);
    int DistributeDataFromStack(const std::string &uuid, const std::string &address, const std::string &randomAddr,
        uint16_t dstPort, const std::vector<uint8_t> &datas);
    void ReleasePortChannel(const std::string &address, uint8_t tcid) const;
    void NotifyDisconnect(const std::string &address);
    bool IsDeviceConnectingOrConnected(const std::string &address) const;
    void HandleConnectEvent(int32_t stat, uint16_t srcPort, uint16_t mtu, AppConnectParamMapping temp);
    void CreateSocketTransferChannel(uint16_t port, const std::string &address, uint16_t mtu, int &fd);
#ifdef RES_SCHED_SUPPORT
    void CheckRssAppState(uint64_t tokenId, uint32_t uid);
    void CheckReportNlConnectStateToRss(DataTransferConnectionParams connectionParams, AppConnectParamMapping temp,
        std::shared_ptr<SleDataTransferCache> cache);
#endif
    static int ReceiveDataCallback(const TRANS_Addr_S *addr, uint8_t *data, uint16_t len);
    static void SendDataStateCallback(const SLE_Addr_S *devAddr, uint8_t tcid, uint16_t portId, uint8_t result);
    static void ChannelStatusCallback(const QOSM_TransChannelRspParams_S *respParams);
    static bool CheckChannelParamCallback(const SLE_Addr_S *addr, uint16_t srcPort);

    std::set<uint16_t> ports_;
    ffrt::mutex flagMutex_;

    enum IsHighSpeedSupportedType {
        HIGH_SPEED_UNKNOWN = 0,
        HIGH_SPEED_SUPPORT = 0x1,
        HIGH_SPEED_NOT_SUPPORT = 0x2
    };

    enum ConnectIntervalType {
        HIGH_SPEED_INTERVAL = 0,
        MID_SPEED_INTERVAL = 0x1,
        LOW_SPEED_INTERVAL = 0x2
    };

    struct DataTransferIntervalMap {
        ConnectIntervalType intervalType;
        const uint16_t intervalValue;
    };
    DECLARE_IMPL();
};
}  // namespace OHOS::Nearlink
#endif  // OHOS_NEARLINK_DATATRANSFER_SERVICE_H