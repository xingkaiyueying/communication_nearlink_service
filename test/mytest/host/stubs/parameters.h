#pragma once
namespace OHOS::system {
inline int mockMaxTerminals = 2;
inline int GetIntParameter(const char *, int) { return mockMaxTerminals; }
}
