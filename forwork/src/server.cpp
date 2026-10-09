#include "server.h"

#include <chrono>
#include <exception>
#include <vector>

#include "log.h"

Server::Server(ServerConfig cfg, Handler handler)
    : cfg_(cfg),
      handler_(std::move(handler)),
      bufPool_(kBufBlockSize),
      pool_(cfg.threads) {
    // 监听 socket（非阻塞，交给 select）
    listenSock_.setReuseAddr(true);
    listenSock_.bind(cfg_.ip, cfg_.port);
    listenSock_.listen(SOMAXCONN);
    listenSock_.setNonBlocking(true);

    // 自管道：工作线程完成响应后写 1 字节，唤醒阻塞在 select 的主线程
    auto pr = net::makeSocketPair();
    wakeRead_ = std::move(pr.first);
    wakeWrite_ = std::move(pr.second);
    wakeRead_.setNonBlocking(true);
    wakeWrite_.setNonBlocking(true);

    FD_ZERO(&readSet_);
    FD_ZERO(&writeSet_);
    FD_SET(listenSock_.get(), &readSet_);
    FD_SET(wakeRead_.get(), &readSet_);
}

Server::~Server() = default;

uint64_t Server::nowMs() const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void Server::run() {
    LOG_INFO("服务器启动 http://%s:%u  线程池=%zu  空闲超时=%llums",
             cfg_.ip.c_str(), cfg_.port, cfg_.threads,
             static_cast<unsigned long long>(cfg_.idleTimeoutMs));

    while (running_) {
        int delay = timer_.nextDelayMs(nowMs());
        timeval tv{};
        timeval* ptv = nullptr;
        if (delay >= 0) {
            tv.tv_sec = delay / 1000;
            tv.tv_usec = (delay % 1000) * 1000;
            ptv = &tv;
        }

        fd_set r = readSet_;
        fd_set w = writeSet_;
        int n = ::select(0, &r, &w, nullptr, ptv);
        if (n < 0) {
            if (WSAGetLastError() == WSAEINTR) continue;
            LOG_ERROR("select 出错 errno=%d", WSAGetLastError());
            break;
        }

        uint64_t now = nowMs();

        // 1. 到期的连接
        for (SOCKET fd : timer_.tick(now)) closeConn(fd);

        // 2. 唤醒：工作线程完成了响应
        if (FD_ISSET(wakeRead_.get(), &r)) {
            drainWakePipe();
            drainResponses();
        }

        // 3. 新连接
        if (FD_ISSET(listenSock_.get(), &r)) acceptAll();

        // 4. 可读连接
        for (auto it = conns_.begin(); it != conns_.end();) {
            SOCKET fd = it->first;
            Conn* c = it->second.get();
            ++it;
            if (c->state == ConnState::Reading && FD_ISSET(fd, &r)) handleRead(fd);
        }

        // 5. 可写连接
        for (auto it = conns_.begin(); it != conns_.end();) {
            SOCKET fd = it->first;
            Conn* c = it->second.get();
            ++it;
            if (c->state == ConnState::Writing && FD_ISSET(fd, &w)) handleWrite(fd);
        }
    }
}

void Server::acceptAll() {
    while (true) {
        SOCKET raw = ::accept(listenSock_.get(), nullptr, nullptr);
        if (raw == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) LOG_ERROR("accept 出错 errno=%d", err);
            break;  // 没有更多连接（或出错）
        }
        net::Socket client(raw);
        client.setNonBlocking(true);
        SOCKET fd = client.get();

        auto conn = std::make_unique<Conn>(bufPool_);
        conn->sock = std::move(client);
        conns_[fd] = std::move(conn);

        FD_SET(fd, &readSet_);
        refreshTimer(fd);
        LOG_DEBUG("新连接 fd=%llu 当前连接数=%zu",
                  static_cast<unsigned long long>(fd), conns_.size());
    }
}

void Server::handleRead(SOCKET fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    Conn& c = *it->second;

    char buf[4096];
    while (true) {
        int n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            if (!c.inBuf.append(buf, n)) {
                LOG_WARN("fd=%llu 请求过大，返回 413",
                         static_cast<unsigned long long>(fd));
                c.keepAlive = false;
                startResponse(fd, HttpResponse::makeText(413, "Payload Too Large"));
                return;
            }
        } else if (n == 0) {
            closeConn(fd);   // 对端关闭
            return;
        } else {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) break;  // 本次数据读完
            closeConn(fd);
            return;
        }
    }

    tryDispatch(fd);
}

void Server::handleWrite(SOCKET fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    Conn& c = *it->second;

    while (c.outOffset < c.outBuf.size()) {
        int n = ::send(fd, c.outBuf.data() + c.outOffset,
                       static_cast<int>(c.outBuf.size() - c.outOffset), 0);
        if (n > 0) {
            c.outOffset += static_cast<size_t>(n);
        } else {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                FD_SET(fd, &writeSet_);  // 下次可写再继续
                return;
            }
            closeConn(fd);
            return;
        }
    }

    // 发送完成
    c.outBuf.clear();
    c.outOffset = 0;
    FD_CLR(fd, &writeSet_);

    if (c.keepAlive) {
        c.state = ConnState::Reading;
        FD_SET(fd, &readSet_);
        refreshTimer(fd);
        tryDispatch(fd);   // 处理可能已缓冲的下一个请求（pipeline）
    } else {
        closeConn(fd);
    }
}

void Server::tryDispatch(SOCKET fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    Conn& c = *it->second;
    if (c.state != ConnState::Reading) return;

    HttpRequest req;
    size_t consumed = 0;
    ParseResult r = HttpRequest::tryParse(c.inBuf.data(), c.inBuf.size(), req, consumed);

    if (r == ParseResult::Ok) {
        c.inBuf.erase(consumed);
        c.keepAlive = req.keepAlive();
        c.state = ConnState::Busy;   // 交给线程池处理，期间主线程不再碰这个 fd
        FD_CLR(fd, &readSet_);
        refreshTimer(fd);

        pool_.submit([this, fd, req = std::move(req)]() mutable {
            HttpResponse resp;
            try {
                resp = handler_(req);
            } catch (const std::exception& e) {
                LOG_ERROR("handler 异常: %s", e.what());
                resp = HttpResponse::makeText(500, "Internal Server Error");
            }
            resp.keepAlive = req.keepAlive();
            {
                std::lock_guard<std::mutex> lk(respMutex_);
                respQueue_.emplace_back(fd, std::move(resp));
            }
            char b = 1;
            ::send(wakeWrite_.get(), &b, 1, 0);   // 唤醒主循环
        });
    } else if (r == ParseResult::Error) {
        LOG_WARN("fd=%llu HTTP 解析失败", static_cast<unsigned long long>(fd));
        closeConn(fd);
    }
    // NeedMore：继续等数据
}

void Server::startResponse(SOCKET fd, HttpResponse resp) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    Conn& c = *it->second;
    c.outBuf = resp.toString();
    c.outOffset = 0;
    c.state = ConnState::Writing;
    FD_CLR(fd, &readSet_);
    handleWrite(fd);   // 立刻尝试非阻塞发送（可能一次发完，也可能注册写事件）
}

void Server::closeConn(SOCKET fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    FD_CLR(fd, &readSet_);
    FD_CLR(fd, &writeSet_);
    timer_.remove(fd);
    conns_.erase(it);   // Socket 析构关闭连接，Buffer 析构归还内存池
}

void Server::refreshTimer(SOCKET fd) {
    uint64_t deadline = nowMs() + cfg_.idleTimeoutMs;
    conns_[fd]->deadline = deadline;
    timer_.add(fd, deadline);
}

void Server::drainWakePipe() {
    char buf[256];
    while (::recv(wakeRead_.get(), buf, sizeof(buf), 0) > 0) {}
}

void Server::drainResponses() {
    std::vector<std::pair<SOCKET, HttpResponse>> batch;
    {
        std::lock_guard<std::mutex> lk(respMutex_);
        batch.reserve(respQueue_.size());
        for (auto& p : respQueue_) batch.push_back(std::move(p));
        respQueue_.clear();
    }
    for (auto& p : batch) {
        startResponse(p.first, std::move(p.second));
    }
}
