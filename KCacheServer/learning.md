# KCacheServer 学习指南

## 项目三层架构

```
┌─────────────────────────────────────────────┐
│         KCacheServer (应用层)                 │
│  main.cc → CacheServer → Protocol            │
│  benchmark.cc                                │
│  作用：TCP 网络 + 命令解析 + 请求分发           │
├─────────────────────────────────────────────┤
│         CacheSystem (缓存策略层)               │
│  CachePolicy.h  (接口)                        │
│  LruCache.h / LfuCache.h / ArcCache.h        │
│  作用：纯数据结构和淘汰算法                     │
├─────────────────────────────────────────────┤
│         muduo-core (网络库层)                  │
│  EventLoop / TcpServer / TcpConnection        │
│  Buffer / Poller (epoll)                      │
│  作用：Reactor 事件驱动 + 非阻塞 I/O            │
└─────────────────────────────────────────────┘
```

## 推荐学习路线（由底向上）

### 第 1 步：CacheSystem（1-2 小时）

这是最独立、最纯粹的部分，不依赖网络知识。

- **CachePolicy.h**（30 行接口）— 先看这个，搞懂 `get`/`put`/`remove` 三个纯虚函数，这就是所有缓存策略的"合同"
- **LruCache.h** — 用 `std::list` + `std::unordered_map` 实现，是最直观的淘汰策略
- **LfuCache.h** — 按访问频率淘汰，比 LRU 多了一个频率计数器
- **ArcCache/**（4 个文件）— 最复杂的自适应算法，初学可以跳过，回头再看

> **核心收获：** 理解"哈希表加速查找 + 双向链表维护淘汰顺序"这个经典组合。

---

### 第 2 步：Protocol（30 分钟）

这是业务层和网络层之间的"翻译官"。

- **Protocol.h** — 看 `Command` 枚举 + `Request` 结构体，理解支持哪些命令
- **Protocol.cc** — 两个核心函数：
  - `parse()` — 怎么从字节流里切出完整的一行命令（`\r\n` 分隔）
  - `encode*()` — 怎么把结果拼成响应字符串

> **核心收获：** 简单的文本协议设计（类似 Redis 的 inline command）。

---

### 第 3 步：CacheServer（1-2 小时）

这是把协议和缓存粘在一起的主逻辑。

- **CacheServer.h** — 先看类结构：成员变量 `server_` / `cache_` / 原子计数器
- **CacheServer.cc** — 三个回调函数是精髓：
  - `onConnection()` — 连上来记个数，断开打日志
  - `onMessage()` — 调 `Protocol::parse` 解包，调 `processCommand` 处理
  - `processCommand()` — 一个 `switch-case`，把命令映射到 `cache_->get/put/remove`

> **核心收获：** 理解 muduo 的回调驱动模型 — "你只管写回调，框架帮你调"。

---

### 第 4 步：main.cc（15 分钟）

把整个启动流程串起来。

- 命令行参数解析 → `createCache()` 工厂函数 → 构造 `CacheServer` → `loop.loop()` 进入事件循环

---

### 第 5 步：muduo-core（按需深入）

不需要通读所有源码。建议先理解这几个核心概念：

| 类 | 一句话角色 |
|---|---|
| EventLoop | 就是一个死循环，反复调 `epoll_wait`，然后分发事件 |
| Channel | 包装一个 fd + 它关心的事件（可读/可写）+ 回调 |
| Poller / EPollPoller | 封装 `epoll_create`/`ctl`/`wait` 系统调用 |
| TcpServer | 管理 Acceptor + 多个 TcpConnection |
| TcpConnection | 一条 TCP 连接的状态机（读 → 回调 → 写 → 回调） |
| Buffer | 应用层的读/写缓冲区，解决粘包问题 |

---

### 第 6 步：benchmark.cc（学完前面再回来看）

用原生 socket + 多线程压测，里面没有用 muduo，是纯 POSIX API。

---

## 动手建议

边看边做，效果最好：

1. **改策略对比** — 分别启动 `--policy lru/lfu/arc/lruk`，用 benchmark 压测，看命中率差异
2. **加一个命令** — 比如 `EXISTS key`（判断 key 是否存在但不返回值），体会从 Protocol → processCommand 的完整链路
3. **调参数** — 改 `--capacity 100` 和 `--capacity 100000`，观察命中率和吞吐量变化
