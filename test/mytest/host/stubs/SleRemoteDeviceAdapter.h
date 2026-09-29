#pragma once
#include "raw_address.h"
namespace OHOS::Nearlink {
inline bool mockSecure = true;
struct SleRemoteDeviceAdapter {
    static SleRemoteDeviceAdapter *GetInstance() { static SleRemoteDeviceAdapter a; return &a; }
    bool IsBondedFromLocal(const RawAddress &) { return mockSecure; }
    bool IsAcbConnected(const RawAddress &) { return mockSecure; }
    bool IsAcbEncrypted(const RawAddress &) { return mockSecure; }
    uint8_t GetPeerDeviceAddrType(const RawAddress &) { return 0; }
};
}
