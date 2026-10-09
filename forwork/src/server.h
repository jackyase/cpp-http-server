#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "buffer.h"
#include "http.h"
#include "memory_pool.h"
#include "net.h"
#include "thread_pool.h"
#include "timer.h"

// 读缓冲块大小（= 单个请求的最大字节数，超过回 413）
constexpr size_t kBufBlockSize = 64 * 1024;

struct ServerConfig {
    std::string ip = "0.0.0.0";
    uint16_t port = 8080;
    size_t threads = 4;              // 线程池大小
    uint64_t idleTimeoutMs = 60000;  // keep-alive 空闲超时（毫秒）
};

enum class ConnState { Reading, Busy, Writing };

// 单个连接的状态
struct Conn {
    explicit Conn(MemoryPool& pool) : inBuf(pool) {}
    net::Socket sock;
    ConnState state = ConnState::Reading;
    Buffer inBuf;          // 读缓冲（内存池分配）
    std::string outBuf;    // 待发送的响应
    size_t outOffset = 0;  // 已发送字节数
    bool keepAlive = false;
    uint64_t deadline = 0; // 到期时间（懒删除用）
};

// Reactor 服务器：select 多路复用 + 线程池 + 定时器 + 内存池
//
// 分工：
//   - 主线程(Reactor) 只做 I/O：select 监听所有连接，读请求、发响应。
//   - 线程池 只做计算：调用 handler 生成响应，通过自管道唤醒主线程。
//   - 定时器 管理连接空闲超时。
class Server {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    Server(ServerConfig cfg, Handler handler);
    ~Server();

    void run();

private:
    void acceptAll();
    void handleRead(SOCKET fd);
    void handleWrite(SOCKET fd);
    void tryDispatch(SOCKET fd);
    void startResponse(SOCKET fd, HttpResponse resp);
    void closeConn(SOCKET fd);
    void refreshTimer(SOCKET fd);
    void drainResponses();
    void drainWakePipe();
    uint64_t nowMs() const;

    ServerConfig cfg_;
    Handler handler_;
    MemoryPool bufPool_;
    Timer timer_;
    net::Socket listenSock_;
    net::Socket wakeRead_;   // 自管道读端（select 监控）
    net::Socket wakeWrite_;  // 自管道写端（工作线程唤醒用）
    fd_set readSet_;
    fd_set writeSet_;
    std::unordered_map<SOCKET, std::unique_ptr<Conn>> conns_;
    std::mutex respMutex_;
    std::deque<std::pair<SOCKET, HttpResponse>> respQueue_;
    ThreadPool pool_;        // 放最后：析构时先 join 工作线程
    bool running_ = true;
};
