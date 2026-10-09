#include "timer.h"

void Timer::add(SOCKET fd, uint64_t deadlineMs) {
    deadlineOf_[fd] = deadlineMs;
    heap_.push({deadlineMs, fd});
}

void Timer::remove(SOCKET fd) {
    deadlineOf_.erase(fd);  // 堆里的旧条目会变成 stale，在 tick 时被跳过
}

std::vector<SOCKET> Timer::tick(uint64_t nowMs) {
    std::vector<SOCKET> expired;
    while (!heap_.empty() && heap_.top().first <= nowMs) {
        Entry e = heap_.top();
        heap_.pop();
        auto it = deadlineOf_.find(e.second);
        if (it != deadlineOf_.end() && it->second == e.first) {
            // 有效条目：确实到期
            deadlineOf_.erase(it);
            expired.push_back(e.second);
        }
        // else: stale 条目（fd 已被删或 deadline 已更新），跳过
    }
    return expired;
}

int Timer::nextDelayMs(uint64_t nowMs) const {
    if (heap_.empty()) return -1;
    uint64_t d = heap_.top().first;
    return static_cast<int>(d > nowMs ? d - nowMs : 0);
}
