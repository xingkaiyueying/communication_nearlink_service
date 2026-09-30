#pragma once
#include <string>
#include <cstdio>
namespace OHOS::Nearlink {
class RawAddress {
    std::string value;
public:
    explicit RawAddress(const std::string &s):value(s) {}
    static RawAddress ConvertToString(const uint8_t *bytes) {
        char value[18] = {};
        snprintf(value, sizeof(value), "%02X:%02X:%02X:%02X:%02X:%02X",
            bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
        return RawAddress(value);
    }
    const std::string &GetAddress() const { return value; }
    void ConvertToUint8(uint8_t *out, int) const {
        unsigned a[6] = {}; sscanf(value.c_str(),"%x:%x:%x:%x:%x:%x",&a[0],&a[1],&a[2],&a[3],&a[4],&a[5]);
        for(int i=0;i<6;++i) out[i]=a[i];
    }
};
}
