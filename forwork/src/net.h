#pragma once

// Windows 的 select 默认只支持 64 个 socket（FD_SETSIZE=64）。
// 要支持更多并发连接，必须在包含 winsock2.h 之前把它调大。
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <string>
#include <utility>

namespace net {

// RAII 封装 WSAStartup / WSACleanup。必须在任何 socket 操作之前构造（main 第一行）。
class WsaGuard {
public:
    WsaGuard();
    ~WsaGuard();
    WsaGuard(const WsaGuard&) = delete;
    WsaGuard& operator=(const WsaGuard&) = delete;
};

// RAII 封装 SOCKET 句柄：析构自动 closesocket，异常安全。
class Socket {
public:
    Socket();
    explicit Socket(SOCKET raw);
    ~Socket();

    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    SOCKET get() const { return fd_; }
    bool valid() const { return fd_ != INVALID_SOCKET; }

    void bind(const std::string& ip, uint16_t port);
    void listen(int backlog);
    Socket accept();                        // 阻塞 accept（阻塞式服务器用）
    void connect(const std::string& ip, uint16_t port);
    void setReuseAddr(bool on);
    void setNonBlocking(bool on);
    uint16_t getLocalPort() const;          // 本端端口（bind port 0 时随机分配）

private:
    SOCKET fd_ = INVALID_SOCKET;
};

// Winsock 没有 socketpair，手动实现一个（用于「工作线程 → 主循环」的唤醒）。
std::pair<Socket, Socket> makeSocketPair();

}  // namespace net
