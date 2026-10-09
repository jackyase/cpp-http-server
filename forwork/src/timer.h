#pragma once

#include <cstdint>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

#include <winsock2.h>  // SOCKET

// 定时器：基于最小堆 + 懒删除，管理连接的过期时间。
// 用 (deadline, fd) 建堆；fd 的 deadline 更新时旧条目留在堆里变成 stale，
// tick 时通过 deadlineOf_ 判断是否有效，避免在堆里做删除。
class Timer {
public:
    void add(SOCKET fd, uint64_t deadlineMs);
    void remove(SOCKET fd);
    std::vector<SOCKET> tick(uint64_t nowMs);  // 返回所有到期 fd
    int nextDelayMs(uint64_t nowMs) const;     // 距最近到期还有多少 ms；无到期返回 -1
    size_t size() const { return deadlineOf_.size(); }

private:
    using Entry = std::pair<uint64_t, SOCKET>;  // (deadline, fd)
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap_;
    std::unordered_map<SOCKET, uint64_t> deadlineOf_;
};
