# MiniHttpServer — 高性能 C++ HTTP 服务器

一个从零手写、**无第三方库**的 Windows 原生 HTTP 服务器，基于 **I/O 多路复用（select）+ Reactor 模式 + 线程池 + 定时器 + 内存池**。适合作为后端 C++ 求职的简历项目。

## 功能特性

- ✅ **HTTP/1.1**：请求解析（请求行 / 头 / body）、响应构造、404 / 413 / 500 处理
- ✅ **I/O 多路复用**：select 实现 Reactor，单个线程管理上千连接
- ✅ **非阻塞 I/O + 增量解析**：数据按块到达也能正确解析，天然处理 TCP 粘包 / 拆包
- ✅ **线程池**：I/O 与计算分离——主线程只做收发，工作线程只跑业务
- ✅ **keep-alive 长连接** + 空闲超时自动断开
- ✅ **定时器**：最小堆 + 懒删除，O(log n) 管理连接过期
- ✅ **内存池**：空闲链表固定块分配，连接读缓冲零 malloc 抖动
- ✅ **线程安全日志**：时间戳 + 线程 id + 分级
- ✅ **8 项自测** + **压测客户端** + **阻塞式对照版**（`--blocking`）

## 技术栈

C++17 · Winsock2 · Visual Studio 2022 · MSBuild（无任何第三方依赖）

## 架构

```
                       ┌────────────── 主线程 (Reactor) ──────────────┐
                       │   select(readSet, writeSet, 自管道, 定时器)    │
                       │                                            │
   客户端 ──连接──▶ listen socket ──accept──▶ Conn（内存池读缓冲）      │
                       │     │                                      │
                       │  可读? ─recv──▶ Buffer ──解析出完整请求        │
                       │                              │              │
                       │                       派发到线程池（fd 标记 Busy）│
                       ▼                              ▼              │
                  ┌──────────┐                ┌───────────────┐      │
                  │ 定时器    │◀── 刷新空闲超时 ──│ 线程池 worker   │      │
                  │ (最小堆)  │                │ handler()      │      │
                  └──────────┘                │ 生成 HttpResponse│      │
                                              └───────┬───────┘      │
                                                      │ 写响应队列 + 自管道唤醒
                                                      ▼               │
                       可写? ─send──▶ 发送完成 ── keep-alive? ──关闭 / 续读
                       └────────────────────────────────────────────┘
```

## 目录结构

```
forwork/
├── forwork.sln                  # VS 解决方案
├── forwork/
│   ├── forwork.vcxproj          # 工程（C++17 + /utf-8 + ws2_32.lib）
│   └── src/
│       ├── net.h/.cpp           # Winsock RAII 封装（Socket / WsaGuard / socketpair）
│       ├── http.h/.cpp          # HTTP 增量解析 + 响应构造
│       ├── buffer.h/.cpp        # 内存池支撑的连接读缓冲
│       ├── memory_pool.h/.cpp   # 固定块内存池（空闲链表）
│       ├── thread_pool.h/.cpp   # 固定大小线程池
│       ├── timer.h/.cpp         # 最小堆定时器（懒删除）
│       ├── log.h/.cpp           # 线程安全日志
│       ├── server.h/.cpp        # Reactor 服务器（核心）
│       └── main.cpp             # 入口（server / --blocking / --selftest / --bench）
├── docs/
│   └── DESIGN.md                # 设计文档 + 面试高频题 Q&A
└── README.md
```

## 快速开始

用 Visual Studio 2022 打开 `forwork.sln`，选 **x64 + Release**，`F5` 运行；或命令行：

```powershell
# 启动服务器（默认 8080 端口，4 线程）
forwork.exe

# 浏览器 / curl 访问
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8080/hello
```

命令行参数：

```
forwork.exe                          # 启动 Reactor 服务器
forwork.exe --blocking               # 阻塞式服务器（对照版）
forwork.exe --selftest               # 运行自测
forwork.exe --bench --conns 100 --reqs 1000   # 压测客户端
forwork.exe --port 9000 --threads 8  # 自定义端口 / 线程数
```

## 压测结果

环境：Windows 11，本机回环（localhost），Release x64，keep-alive 长连接，请求 `GET /`。

| 并发连接 × 每连接请求 | 总请求 | 失败 | 吞吐 |
|---|---|---|---|
| 100 × 1000 | 10 万 | 0 | **~1.9 万 req/s** |
| 500 × 200 | 10 万 | 0 | ~1.6 万 req/s |

> 说明：回环压测主要反映本机 CPU 与调度开销；真实瓶颈常在网络带宽和内核。压测客户端 `--bench` 会为每条连接开一个线程。

## 设计说明

架构原理、为什么这么设计、以及这个项目能回答的面试题，见 **[docs/DESIGN.md](docs/DESIGN.md)**。

## 后续可做（Roadmap）

- [ ] 用 **IOCP** 重写 I/O 层（Windows 上对标 epoll 的完成端口模型）
- [ ] 支持静态文件 / 目录浏览
- [ ] HTTP 方法扩展（POST 表单、简单 CGI）
- [ ] 支持 `select` → `epoll`（Linux/WSL）移植，抽离 I/O 复用接口
