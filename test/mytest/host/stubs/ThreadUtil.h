#pragma once
#include <deque>
#include <functional>
namespace OHOS::Nearlink {
inline std::deque<std::function<void()>> tasks;
inline void DoInIpShareThread(std::function<void()> fn) { tasks.push_back(fn); }
inline void DrainTasks() { while (!tasks.empty()) { auto fn=tasks.front(); tasks.pop_front(); fn(); } }
}
