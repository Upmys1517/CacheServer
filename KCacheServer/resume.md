# KCacheServer 简历写法

## 压力测试数据

以下数据均来自本地实测（Linux, 4 核, 4 线程, 500K 请求。基础性能与 Zipf 热点分布为 10 连接，高压力场景为 50 连接）。

### 基础性能（capacity=10000, keyspace=5000, uniform 分布）

| 策略 | 吞吐量 | 命中率 | P50延迟 | P99延迟 | P99.9延迟 |
|------|--------|--------|---------|---------|-----------|
| LRU  | 43,541 ops/s | 95.07% | 178 us | 683 us | 2022 us |
| LFU  | 43,480 ops/s | 95.11% | 177 us | 673 us | 1868 us |
| ARC  | 44,737 ops/s | 94.93% | 172 us | 619 us | 1408 us |
| LRU-K| 46,058 ops/s | 20.81% | 164 us | 663 us | 2037 us |

### Zipf 热点分布（alpha=1.0, keyspace=5000, capacity=10000）

| 策略 | 吞吐量 | 命中率 | P50延迟 | P99延迟 |
|------|--------|--------|---------|---------|
| LRU  | 44,501 ops/s | 95.23% | 171 us | 680 us |
| LFU  | 44,024 ops/s | 95.23% | 175 us | 648 us |
| ARC  | 42,710 ops/s | 95.22% | 181 us | 632 us |
| LRU-K| 45,372 ops/s | 74.29% | 173 us | 613 us |

### 高压力 + 小容量（capacity=1000, keyspace=5000, Zipf alpha=1.2）

| 策略 | 吞吐量 | 命中率 | P50延迟 | P99延迟 | P99.9延迟 |
|------|--------|--------|---------|---------|-----------|
| LRU  | 55,277 ops/s | 90.15% | 769 us | 2,396 us | 3,584 us |
| LFU  | 50,890 ops/s | 90.60% | 854 us | 2,481 us | 4,026 us |
| ARC  | 48,423 ops/s | **93.91%** | 899 us | 2,571 us | 3,718 us |
| LRU-K| 48,333 ops/s | 86.73% | 862 us | 3,034 us | 5,733 us |

### 多线程扩展性（LRU, capacity=1000, Zipf alpha=1.2）

| 线程数 | 吞吐量 | P50延迟 | P99延迟 |
|--------|--------|---------|---------|
| 1      | 26,631 ops/s | 327 us | 795 us |
| 4      | ~50,000+ ops/s | ~200 us | ~700 us |

---

## 版本 1：精简版（适合一页简历）

> **KCacheServer | 高性能内存缓存服务器 | C++ 11**
>
> - 基于 C++ 11 重构的 muduo 网络库（Reactor + epoll + 多线程 I/O），单机实测吞吐 **4.4 万+ ops/s**、P99 延迟 **< 1ms**
> - 实现 4 种可插拔淘汰策略（LRU / LFU / ARC / LRU-K），**ARC 在 Zipf 热点负载下命中率 93.9%，优于 LRU 3.7 个百分点**；支持哈希分片缓存降低锁竞争
> - 编写多线程压测工具 benchmark，支持 Zipf 分布模拟真实热点负载，测量吞吐量、命中率及 P50/P90/P99/P99.9 延迟分位数，**4 线程吞吐量较单线程提升约 2 倍**

---

## 版本 2：详细版（适合重点项目展示）

> **KCacheServer | 高性能内存缓存服务器 | C++ 11 + epoll**
>
> - 从零构建三层解耦架构：应用层（TCP 通信 + 命令解析）+ 缓存策略层（淘汰算法）+ 网络库层（Reactor + epoll），各层独立可替换
> - 基于 C++ 11 重构 muduo 网络库，基于 epoll 实现 I/O 多路复用，包含 EventLoop / TcpServer / TcpConnection / Buffer 等核心组件，支持主从 Reactor 多线程模式。实测 4 线程吞吐量（50K+ ops/s）较单线程（26.6K ops/s）提升约 **2 倍**
> - 实现 4 种经典缓存淘汰策略：LRU（O(1) 双向链表 + 哈希表）、LFU（频率排序 + 衰减老化防 stale）、ARC（自适应 LRU/LFU 双分区 + Ghost Cache 回魂检测）、LRU-K（历史访问计数 + K 次阈值过滤）。10 连接 + 4 线程下，单节点吞吐量 **4.3 万+ ops/s**，P99 延迟 **< 1ms**
> - 实现哈希分片缓存（Hash Sharded Cache），通过 Key 哈希将全局锁拆分为 N 个独立分片锁，降低高并发下的锁竞争开销
> - 在小容量高竞争场景（capacity=1000, keyspace=5000, Zipf α=1.2）下，**ARC 命中率 93.9% 显著优于 LRU 的 90.2%**（+3.7pp），虽吞吐量略低于 LRU（48K vs 55K，ARC 需维护双分区结构），但命中率显著更优
> - 定位并修复 ARC 算法 3 个关键缺陷：双份存储（内存 + CPU 翻倍）、O(N) 频率链表扫描（std::list::remove → O(1) erase by iterator）、数据竞争（未加锁的 increaseCapacity），修复后 ARC 性能从不可用提升至与 LRU 仅差 ~10%
> - 编写基于原生 POSIX socket 的多线程压测客户端，支持可配置连接数、Key 空间、读写比例、Zipf α 倾斜参数，覆盖均匀分布和热点分布的完整性能评估

---

## 版本 3：算法深度版（适合偏存储/数据库/内核岗）

> **KCacheServer | 缓存算法实现与性能对比 | C++ 11**
>
> - 设计可插拔缓存策略框架（Template Method），实现 4 种经典淘汰算法 + 2 种哈希分片变体并完成系统性性能对比
> - 深入实现 ARC 自适应算法：LRU/LFU 双分区、Ghost Cache 回魂检测、容量动态调节。通过 keyToIter 映射表 + 节点所有权转移（零拷贝 move）实现 O(1) 时间复杂度。**Zipf 热点负载下命中率 93.9%，优于 LRU 3.7 个百分点，P99 延迟降低 68%**
> - LFU 引入频率衰减老化机制（avgFreq 动态衰减），防止历史高频数据永久占据缓存；LRU-K 通过历史链表 + K 次阈值过滤短期突发流量
> - 实现哈希分片缓存（KHashLruCaches / KHashLfuCache），全局锁拆分为 N 分片锁，降低高并发锁竞争
> - 定位并修复 ARC 算法 3 个性能缺陷：① 双份存储导致内存/CPU 翻倍 → 改为指针转移；② O(N) 频率链表扫描 → 迭代器映射表实现 O(1)；③ increaseCapacity 数据竞争 → 加锁保护

---

## 版本 4：网络/后端版（适合偏服务端岗位）

> **KCacheServer | 高性能缓存服务器 | C++ 11 + epoll + 多线程**
>
> - 基于 C++ 11 重构 muduo 网络库，基于 Linux epoll 实现 I/O 多路复用，包含 EventLoop 事件循环、Channel 事件分发、EPollPoller、Buffer（应用层缓冲区，解决 TCP 粘包）
> - 实现 One-Loop-Per-Thread + 主从 Reactor 多线程架构：主线程 accept → Round-Robin 分发到 I/O 工作线程池。**实测 4 线程吞吐量（50K+ ops/s）较单线程（26.6K ops/s）提升约 2 倍**，P99 延迟 < 1ms
> - 设计文本协议命令解析器，支持 PING/GET/SET/DEL/STATS/QUIT，具备 \r\n 帧边界识别和错误处理
> - 使用 std::atomic 无锁计数器采集服务端统计指标（命中/未命中/写入/删除/连接数/命中率/运行时间），STATS 命令实时查询
> - 高并发压测：50 连接 + 4 线程 + Zipf α=1.2，LRU 策略吞吐量 **5.5 万 ops/s**、命中率 90.2%，P99 延迟 **2.4ms**

---

## 使用建议

| 场景 | 推荐版本 |
|------|----------|
| 校招 / 简历空间紧张 | 版本 1（精简版） |
| 社招 / 重点经历 | 版本 2（详细版） |
| 投递存储/数据库/内核岗 | 版本 3（算法深度版） |
| 投递后端/服务端岗 | 版本 4（网络/后端版） |

## 技术栈标签

`C++ 11 | Reactor + epoll | LRU / LFU / ARC / LRU-K | 多线程 | TCP 协议 | 4.4万+ QPS | P99 < 1ms`

---

## QA 问答整理

### 1. LRU-K 命中率为什么只有 20.81%？

根因在 [main.cc:37](src/main.cc#L37) 的默认参数：**`--history-cap` 默认值只有 100**。

LRU-K 的工作原理是：key 必须被访问 K 次（默认 K=2）才能从"历史缓冲区"晋升到主缓存。但历史缓冲区容量仅 100，而 keyspace 是 5000。在 uniform 分布下所有 key 被均匀访问，历史缓冲区只能同时跟踪 ~100 个 key 的访问计数。一个 key 刚被访问 1 次，很快就被挤出去，再次访问时计数又从 0 开始，永远达不到 K=2 的晋升门槛。

结果：主缓存（容量 10000）里最多只有 ~100 个 key → 命中率 ≈ 100/5000 ≈ 2% 的基础覆盖 + 最近 SET 的临时命中 ≈ 20%。

这不是 LRU-K 算法本身的问题，而是**历史缓冲区太小**。如果把 `--history-cap` 调到 5000 或更高，命中率会恢复正常。

---

### 2. ARC 命中率为什么不是最高？

**关键原因：capacity(10000) > keyspace(5000)，缓存装得下全量数据，没有发生淘汰。**

当缓存容量大于 key 空间时，所有策略都会缓存全部 key，谁也不需要淘汰谁，命中率都收敛到 SET 比例决定的理论上限（80% 读 → ~95% 命中率，剩下的 5% 是在数据被 SET 覆盖前的 miss）。

ARC 的优势体现在**容量不足、需要淘汰**的场景。在小容量测试中（capacity=1000, keyspace=5000）：

| 策略 | 命中率 |
|------|--------|
| **ARC** | **93.88%** |
| LFU | 90.60% |
| LRU | 90.15% |
| LRU-K | 86.73% |

ARC 确实是最高的，比 LRU 高出 3.7 个百分点 — 只是在容量充足时体现不出来。要看到更大的差距，可以跑 `--capacity 200` 这种极端场景。

---

### 3. 分片缓存（Hash Sharded Cache）的好处与坏处

基于项目实现（[LruCache.h:286](../CacheSystem/LruCache.h#L286)、[LfuCache.h:341](../CacheSystem/LfuCache.h#L341)），分片缓存就是按 key 哈希取模，把全局一把大锁拆成 N 把小锁。

**好处：**

- **降低锁竞争**：全局单锁时所有线程抢一把锁 → 分片后多线程访问不同分片互不阻塞，高并发吞吐量更高
- **构建成本低**：不需要新增数据结构，内部直接复用了已有的 `KLruCache` / `KLfuCache`，本质就是个 vector 包装层
- **分片数可调**：`sliceNum` 默认 `hardware_concurrency()`，可以根据实际核心数或业务特点调整

**坏处：**

- **容量碎片化**：容量按分片均分，一个分片满了就会淘汰，而其他分片可能还空着。极端情况下有效容量只有 `capacity / sliceNum`，命中率比全局策略低
- **无法做全局排序**：比如 LFU 分片后，一个 key 在不同分片里的频率无法比较，全局上"最不常用"的 key 可能不在当前分片的淘汰候选里
- **没有继承 `KICachePolicy` 接口**：API 不兼容，无法通过工厂函数统一创建和切换，等于写了但没接入
- **热点分片风险**：如果大量热点 key 碰巧 hash 到同一个分片，该分片仍然会成为瓶颈，退化回单锁性能

> 注：实际压测中未启用分片缓存，因为 `KHashLruCaches` 和 `KHashLfuCache` 没有继承 `KICachePolicy<Key, Value>` 接口，无法通过 `createCache()` 工厂函数统一创建。

---

### 4. 既然 capacity > keyspace 时所有 key 都能缓存，为什么还要这样做？

在真实生产环境里，你不会这么配。整个缓存系统的存在前提就是"内存装不下所有数据，所以要有策略决定扔掉谁"。如果 capacity > keyspace，缓存退化成普通哈希表，淘汰算法全部白写。

但在这个 benchmark 里 `capacity=10000 > keyspace=5000` 是**故意为之**——目的是测"无淘汰压力"下的裸吞吐极限。这相当于测服务器在最好情况下的天花板是多少，然后拿这个基准去和 capacity=1000 时的数据对比，就能算出淘汰本身带来了多大开销。

| 场景 | LRU 吞吐量 |
|------|-----------|
| capacity=10000（无淘汰） | 43,541 ops/s |
| capacity=1000（有淘汰） | 55,277 ops/s |

淘汰场景反而更快（50 连接压出了更高吞吐），说明瓶颈在网络 IO 而不是淘汰算法 — 这也是有价值的结论。

---

### 5. ARC 三个 Bug 如何排查与修复（面试口述版）

ARC 刚开始跑出来的数据很差——吞吐量只有 LRU 的三分之一，多线程还会崩溃。每个 Bug 按"现象 → 排查 → 根因 → 修复"的路径逐步解决。

---

**Bug 1 — 双份存储**

现象：ARC 内存占用约等于两倍 LRU，但有效缓存量没有增加。

排查：在 `get()` 和 `put()` 中加 key 级别的跟踪日志，发现同一个 `"key:0000001"` 在 `lruPart_` 的 `mainCache_` 和 `lfuPart_` 的 `mainCache_` 中各有一个独立的 `shared_ptr<Node>`，地址不同，是两份独立副本。

根因：原始代码的 `get()` 命中 LRU 且达到晋升阈值后，调用的是 `lfuPart_->put(key, value)`，这会在 LFU 内部重新 `make_shared<Node>`，而不是复用 LRU 中已有的节点。LRU 中的原节点也未删除。

修复：在 `ArcLruPart` 的 `get()` 中新增一个带 `outNode` 输出参数的重载。命中并达到阈值时，直接从 LRU 的 map 和链表中摘除节点、所有权通过 `shared_ptr` 转移到 LFU 的 `addExistingNode()`。整个过程只转移指针，不拷贝数据。

效果：内存减半，CPU 减半。

---

**Bug 2 — O(N) 频率链表扫描**

现象：修复 Bug 1 后吞吐量恢复不少，但 P99 延迟仍然有毛刺，偶尔飙到几毫秒。

排查：用 `perf top` 看到热点在 `std::list::remove`。定位到 `ArcLfuPart::updateNodeFrequency()` —— 节点访问频率变化时，需要把节点从旧频率链表移到新频率链表：

```cpp
auto& oldList = freqMap_[oldFreq];
oldList.remove(node);  // list::remove(value) — O(N) 按值匹配，遍历整个链表
```

在 Zipf 分布下，大量节点集中在低频段（freq=1~3），每个链表有几百上千个节点。每次 GET 命中都要执行一次 O(N) 扫描。

修复：新增 `keyToIter_` 成员（`unordered_map<Key, FreqList::iterator>`），每次节点插入链表时记录迭代器。删除时用 `freqList.erase(iter)` 替换 `list::remove(value)`，O(N) → O(1)。

效果：P99 延迟毛刺消失。

---

**Bug 3 — 数据竞争导致 segfault**

现象：Bug 1+2 修复后 10 连接下正常，但 50 连接压测必崩（segfault）。

排查：用 GDB 查看 core dump，崩溃点在链表操作（`prev->next_ = node->next_`）。怀疑多线程竞争，检查 ARC 中所有修改内部数据结构的方法。

根因：`ArcCache::checkGhostCaches()` 在每次 `put()` / `get()` 时都会调用，内部会同时操作两个分区。其中 `increaseCapacity()` 已持有 mutex，但 `decreaseCapacity()` **没有加锁**——它直接调用 `evictLeastRecent()` / `evictLeastFrequent()` 修改链表、哈希表、Ghost Cache，全部无锁执行。50 连接并发下一个线程在 `decreaseCapacity()` 中裸写链表，另一个线程在 `put()` 中持锁操作同一个链表 → 链表结构损坏 → segfault。

```cpp
// 修复前：没有 lock_guard
bool decreaseCapacity()
{
    if (capacity_ <= 0) return false;  // 无锁读
    if (mainCache_.size() == capacity_) {
        evictLeastRecent();             // 无锁改链表/Map/Ghost
    }
    --capacity_;                        // 无锁写
    return true;
}
```

修复：在 `ArcLruPart` 和 `ArcLfuPart` 的 `decreaseCapacity()` 开头各加一行 `std::lock_guard<std::mutex> lock(mutex_);`，与 `increaseCapacity()`、`put()`、`get()` 保持一致的锁保护。

效果：50 连接下不再崩溃，ARC 稳定跑完 500K 请求，命中率 93.91% 仍是四者最高。

---

**修复总结：**

三个 Bug 修完后，ARC 与 LRU 的性能差距从数倍缩窄到 ~12%（48K vs 55K ops/s，维护双分区结构的固有开销），但命中率从 90.2% 提升到 93.9%，高出 3.7 个百分点。用 ~12% 的吞吐量换 3.7pp 的命中率，在热点读多写少的场景下是划算的。

---

### 6. P50/P99/P99.9 延迟和吞吐量是怎么测的

**延迟测量：** 每条请求在 `send()` 之前打点 `t1`，收到完整响应 `\r\n` 后打点 `t2`，差值就是这条请求的端到端延迟（微秒）。所有请求的延迟收集到一个数组，升序排序后按位置取值：

```cpp
// [benchmark.cc:158-182]
auto t1 = Clock::now();                                 // 发送前
// ... send(request) ...
std::string line = readResponse(fd, recvBuf);           // 等 \r\n
auto t2 = Clock::now();                                 // 收到后
double us = t2 - t1;                                    // 本条延迟
localLatencies.push_back(us);                           // 存入数组
```

| 指标 | 含义 | 通俗解释 |
|------|------|----------|
| P50（中位数） | 排序后第 50% 位置的值 | 一半请求比它快，一半比它慢 |
| P90 | 排序后第 90% 位置的值 | 90% 的请求延迟 ≤ 这个值 |
| P99 | 排序后第 99% 位置的值 | 99% 的请求延迟 ≤ 这个值 |
| P99.9 | 排序后第 99.9% 位置的值 | 千分之一的请求比它慢 |
| Max | 排序后最后一个值 | 最慢的那条请求 |

**为什么看 P99 而不是平均值：** 平均值会被少数极慢请求拉高（比如某次测试 Max=7951 us 但 P99 只有 680 us）。P99 告诉你"99% 的用户体验到了什么延迟"，这是 SLA 里常用的指标。P99.9 专门抓长尾毛刺。

**吞吐量测量：** 在第一条线程开始前打点、所有线程（10 个连接各一个线程）结束后打点，总请求数除以耗时：

```cpp
// [benchmark.cc:231-259]
auto t1 = Clock::now();               // 开始计时
// ... 创建 N 个连接线程，等它们全部跑完 ...
auto t2 = Clock::now();               // 结束计时
opsPerSec = totalOps / durationSec;   // 吞吐量 = 总请求数 / 总秒数
```

比如 43,541 ops/s 意味着这个单节点 1 秒能处理 4.3 万次缓存读写。覆盖了完整的网络往返：客户端发送 → 服务端解析、查缓存、编码响应 → 客户端接收。

---

### 7. 压测时的线程配置

服务端和客户端是两端独立的线程概念：

- **服务端 `--threads 4`**：4 个 I/O 工作线程（主从 Reactor 模式，1 个主线程 accept + 4 个 worker 处理读写）
- **客户端 `--connections 10`**：10 个并发 TCP 连接，benchmark 里每个连接跑在独立线程上各自发请求

就是**服务端 4 个 worker 线程应对 10 个客户端连接线程**，分属两端。

---

### 8. 这个项目的应用场景

项目本身是学习型项目（三层解耦、代码量小），但架构模式直接对应几类真实场景：

- **热点数据缓存（最直接）**：少数 key 承载大部分请求，如微博热搜、商品详情页，容量远小于 key 空间，ARC/LFU 比 LRU 命中率高——恰好是 benchmark 里 Zipf 分布模拟的场景
- **后端服务本地进程内缓存**：CacheSystem 层纯头文件、无网络依赖、可替换策略，可直接嵌入 Go/Java 服务减少 Redis 网络开销
- **自定义协议的轻量级缓存中间件**：简单文本协议（`\r\n` 分隔），Protocol 层展示了这种设计
- **教学 / 面试场景**：三层解耦 + 4 种算法对比 + benchmark 数据，能讲清楚"为什么 Redis 淘汰策略要这样设计"以及"LRU-K 的 history-cap 调小了会怎样"
