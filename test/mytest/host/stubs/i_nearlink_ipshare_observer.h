#pragma once
#include <memory>
#include "nearlink_ipshare_status.h"
namespace OHOS {
template<class T> using sptr = std::shared_ptr<T>;
namespace Nearlink {
struct INearlinkIpShareObserver { virtual ~INearlinkIpShareObserver() = default; virtual void OnStatusChanged(const NearlinkIpShareStatus &) = 0; };
}}
