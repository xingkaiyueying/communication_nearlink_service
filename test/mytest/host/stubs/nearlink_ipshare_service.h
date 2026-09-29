#pragma once
#include <cstdint>
namespace OHOS::Nearlink {
struct NearlinkIpShareService {
    static bool CanSend(uint16_t, uint8_t, uint8_t, uint64_t);
};
}
