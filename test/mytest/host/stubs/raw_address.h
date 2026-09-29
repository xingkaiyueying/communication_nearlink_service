#pragma once
#include <string>
#include <cstdio>
namespace OHOS::Nearlink {
class RawAddress {
    std::string value;
public:
    explicit RawAddress(const std::string &s):value(s) {}
    void ConvertToUint8(uint8_t *out, int) const {
        unsigned a[6] = {}; sscanf(value.c_str(),"%x:%x:%x:%x:%x:%x",&a[0],&a[1],&a[2],&a[3],&a[4],&a[5]);
        for(int i=0;i<6;++i) out[i]=a[i];
    }
};
}
