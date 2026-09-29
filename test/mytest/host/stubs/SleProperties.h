#pragma once
#include "sdf_addr.h"
namespace OHOS::Nearlink {
struct SleProperties {
    static SleProperties &GetInstance() { static SleProperties p; return p; }
    SLE_Addr_S GetLocalSleAddress() { return {{2,6,7,8,9,10},0}; }
};
}
