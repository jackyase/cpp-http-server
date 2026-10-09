#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "http.h"
#include "log.h"
#include "memory_pool.h"
#include "net.h"
#include "server.h"
#include "thread_pool.h"
#include "timer.h"

namespace {

// 共享的路由处理逻辑（业务层，和网络层解耦）
HttpResponse handle(const HttpRequest& req) {
    if (req.path == "/") {
        return HttpResponse::makeHtml(200,
            "<h1>Hello from my C++ HTTP server!</h1>"
            "<p>select + Reactor + 线程池 + 定时器 + 内存池</p>");
    }
    if (req.path == "/hello") {
        return HttpResponse::makeText(200, "Hello, World!\n");
    }
    return HttpResponse::make404();
}

// 阻塞式服务器（对比用）：一次只处理一个连接
void runBlocking(uint16_t port) {
    net::Socket listen;
    listen.setReuseAddr(true);
    listen.bind("0.0.0.0", port);
    listen.listen(SOMAXCONN);
    std::cout << "[blocking] 监听 http://127.0.0.1:" << port << "\n";

    while (true) {
        net::Socket conn = listen.accept();  // 阻塞，一次一个连接
        std::string raw;
        char buf[4096];
        size_t sep = std::string::npos;
        while ((sep = raw.find("\r\n\r\n")) == std::string::npos) {
            int n = ::recv(conn.get(), buf, sizeof(buf), 0);
            if (n <= 0) break;
            raw.append(buf, n);
            if (raw.size() > 64 * 1024) break;
        }
        if (sep == std::string::npos) continue;

        HttpRequest req;
        size_t consumed = 0;
        if (HttpRequest::tryParse(raw.data(), raw.size(), req, consumed) != ParseResult::Ok) {
            continue;
        }
        std::string data = handle(req).toString();
        size_t off = 0;
        while (off < data.size()) {
            int n = ::send(conn.get(), data.data() + off,
                           static_cast<int>(data.size() - off), 0);
            if (n <= 0) break;
            off += static_cast<size_t>(n);
        }
        // conn 析构关闭
    }
}

// ---- 自测 ----
int g_pass = 0, g_fail = 0;
void check(bool ok, const char* name) {
    if (ok) { ++g_pass; std::cout << "[PASS] " << name << "\n"; }
    else    { ++g_fail; std::cout << "[FAIL] " << name << "\n"; }
}

void testMemoryPool() {
    MemoryPool pool(64, 8);
    std::vector<void*> ptrs;
    for (int i = 0; i < 1000; ++i) ptrs.push_back(pool.allocate());
    for (void* p : ptrs) pool.deallocate(p);

    void* a = pool.allocate();
    void* b = pool.allocate();
    pool.deallocate(a);
    void* c = pool.allocate();
    check(a == c, "内存池复用空闲块");
    pool.deallocate(b);
    pool.deallocate(c);
}

void testTimer() {
    Timer t;
    t.add(1, 100);
    t.add(2, 50);
    t.add(3, 200);
    auto v = t.tick(75);
    check(v.size() == 1 && v[0] == 2, "定时器按到期顺序触发");

    t.remove(3);
    v = t.tick(1000);
    check(v.size() == 1 && v[0] == 1, "定时器懒删除生效");
}

void testHttpParser() {
    HttpRequest req;
    size_t consumed = 0;
    std::string r1 = "GET /hello HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello";
    auto res = HttpRequest::tryParse(r1.data(), r1.size(), req, consumed);
    check(res == ParseResult::Ok, "HTTP 解析完整请求");
    check(req.method == "GET" && req.path == "/hello" && req.body == "hello",
          "HTTP 字段正确");

    std::string r2 = "GET / HTTP/1.1\r\nHost: x\r\n";  // 缺 \r\n\r\n
    check(HttpRequest::tryParse(r2.data(), r2.size(), req, consumed) == ParseResult::NeedMore,
          "HTTP 不完整请求 -> NeedMore");
    check(req.keepAlive(), "HTTP/1.1 默认 keep-alive");
}

void testThreadPool() {
    std::atomic<int> counter{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 1000; ++i) pool.submit([&counter] { counter++; });
    }  // 析构 join 所有线程
    check(counter == 1000, "线程池执行所有任务");
}

void runSelfTest() {
    std::cout << "===== 自测开始 =====\n";
    testMemoryPool();
    testTimer();
    testHttpParser();
    testThreadPool();
    std::cout << "===== 自测结束: " << g_pass << " 通过, " << g_fail << " 失败 =====\n";
}

// ---- 压测客户端 ----
bool sendAll(SOCKET fd, const std::string& s) {
    size_t off = 0;
    while (off < s.size()) {
        int n = ::send(fd, s.data() + off, static_cast<int>(s.size() - off), 0);
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

bool readResponse(SOCKET fd) {
    std::string raw;
    char buf[8192];
    size_t sep = std::string::npos;
    while ((sep = raw.find("\r\n\r\n")) == std::string::npos) {
        int n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        raw.append(buf, n);
    }
    size_t cl = 0;
    size_t p = raw.find("Content-Length:");
    if (p != std::string::npos) {
        p += 15;
        size_t e = raw.find("\r\n", p);
        cl = std::stoul(raw.substr(p, e - p));
    }
    size_t bodyStart = sep + 4;
    while (raw.size() < bodyStart + cl) {
        int n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        raw.append(buf, n);
    }
    return true;
}

void runBench(const std::string& host, uint16_t port, int conns, int reqsPerConn) {
    std::atomic<uint64_t> done{0};
    std::atomic<uint64_t> failed{0};
    auto t0 = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(conns);
    for (int c = 0; c < conns; ++c) {
        threads.emplace_back([&, host, port, reqsPerConn]() {
            net::Socket sock;
            try {
                sock.connect(host, port);
            } catch (...) {
                ++failed;
                return;
            }
            std::string req = "GET / HTTP/1.1\r\nHost: bench\r\n\r\n";
            for (int i = 0; i < reqsPerConn; ++i) {
                if (!sendAll(sock.get(), req)) { ++failed; break; }
                if (!readResponse(sock.get())) { ++failed; break; }
                ++done;
            }
        });
    }
    for (auto& t : threads) t.join();

    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();
    double qps = sec > 0 ? done / sec : 0;
    std::cout << "并发连接: " << conns << ", 每连接请求: " << reqsPerConn << "\n";
    std::cout << "完成: " << done << ", 失败: " << failed << "\n";
    std::cout << "耗时: " << sec << "s, 吞吐: " << static_cast<uint64_t>(qps) << " req/s\n";
}

void usage() {
    std::cout <<
        "用法:\n"
        "  forwork.exe                          # 启动 Reactor 服务器 (8080)\n"
        "  forwork.exe --blocking               # 启动阻塞式服务器（对比）\n"
        "  forwork.exe --selftest               # 运行自测\n"
        "  forwork.exe --bench --conns 100 --reqs 1000 [--port 8080]\n"
        "参数:\n"
        "  --port N     监听/连接端口（默认 8080）\n"
        "  --threads N  线程池大小（默认 4）\n"
        "  --conns N    压测并发连接数\n"
        "  --reqs N     每连接请求数\n";
}

}  // namespace

int main(int argc, char** argv) {
    uint16_t port = 8080;
    size_t threads = 4;
    std::string mode = "server";
    std::string benchHost = "127.0.0.1";
    int benchConns = 100, benchReqs = 1000;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--blocking") mode = "blocking";
        else if (a == "--selftest") mode = "selftest";
        else if (a == "--bench") mode = "bench";
        else if (a == "--port" && i + 1 < argc) port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (a == "--threads" && i + 1 < argc) threads = static_cast<size_t>(std::stoi(argv[++i]));
        else if (a == "--host" && i + 1 < argc) benchHost = argv[++i];
        else if (a == "--conns" && i + 1 < argc) benchConns = std::stoi(argv[++i]);
        else if (a == "--reqs" && i + 1 < argc) benchReqs = std::stoi(argv[++i]);
        else { std::cout << "未知参数: " << a << "\n"; usage(); return 1; }
    }

    net::WsaGuard wsa;

    if (mode == "selftest") { runSelfTest(); return 0; }
    if (mode == "bench")    { runBench(benchHost, port, benchConns, benchReqs); return 0; }
    if (mode == "blocking") { runBlocking(port); return 0; }

    ServerConfig cfg;
    cfg.port = port;
    cfg.threads = threads;
    Server server(cfg, handle);
    server.run();
    return 0;
}
