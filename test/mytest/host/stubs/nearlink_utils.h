#pragma once
#include <string>
namespace OHOS::Nearlink { inline bool IsValidAddress(const std::string &s) { return s.size()==17; } }
