#include "net.h"

#include <stdexcept>

namespace net {

WsaGuard::WsaGuard() {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("WSAStartup 失败");
    }
}

WsaGuard::~WsaGuard() {
    WSACleanup();
}

Socket::Socket() {
    fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd_ == INVALID_SOCKET) {
        throw std::runtime_error("socket() 失败，错误码: " +
                                 std::to_string(WSAGetLastError()));
    }
}

Socket::Socket(SOCKET raw) : fd_(raw) {}

Socket::~Socket() {
    if (fd_ != INVALID_SOCKET) closesocket(fd_);
}

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = INVALID_SOCKET;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        if (fd_ != INVALID_SOCKET) closesocket(fd_);
        fd_ = other.fd_;
        other.fd_ = INVALID_SOCKET;
    }
    return *this;
}

void Socket::setReuseAddr(bool on) {
    int opt = on ? 1 : 0;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
}

void Socket::setNonBlocking(bool on) {
    u_long mode = on ? 1 : 0;
    if (ioctlsocket(fd_, FIONBIO, &mode) != 0) {
        throw std::runtime_error("ioctlsocket(FIONBIO) 失败，错误码: " +
                                 std::to_string(WSAGetLastError()));
    }
}

void Socket::bind(const std::string& ip, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (ip.empty() || ip == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
            throw std::runtime_error("非法 IP 地址: " + ip);
        }
    }
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        throw std::runtime_error("bind() 失败，错误码: " +
                                 std::to_string(WSAGetLastError()));
    }
}

void Socket::listen(int backlog) {
    if (::listen(fd_, backlog) == SOCKET_ERROR) {
        throw std::runtime_error("listen() 失败，错误码: " +
                                 std::to_string(WSAGetLastError()));
    }
}

Socket Socket::accept() {
    SOCKET conn = ::accept(fd_, nullptr, nullptr);
    if (conn == INVALID_SOCKET) {
        throw std::runtime_error("accept() 失败，错误码: " +
                                 std::to_string(WSAGetLastError()));
    }
    return Socket(conn);
}

void Socket::connect(const std::string& ip, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        throw std::runtime_error("非法 IP 地址: " + ip);
    }
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        throw std::runtime_error("connect() 失败，错误码: " +
                                 std::to_string(WSAGetLastError()));
    }
}

uint16_t Socket::getLocalPort() const {
    sockaddr_in addr{};
    int len = sizeof(addr);
    if (getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

std::pair<Socket, Socket> makeSocketPair() {
    Socket listener;
    listener.setReuseAddr(true);
    listener.bind("127.0.0.1", 0);  // 端口 0 = 由系统随机分配
    listener.listen(1);

    Socket client;
    client.connect("127.0.0.1", listener.getLocalPort());

    Socket server = listener.accept();
    return {std::move(client), std::move(server)};
}

}  // namespace net
