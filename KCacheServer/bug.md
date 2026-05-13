Bug 1：双份存储（内存翻倍 + CPU 翻倍）
现象：ARC 模式下，内存占用是 LRU 的 2 倍，吞吐量却只有 LRU 的 1/3。

排查：加日志发现同一个 key 在 lruPart 和 lfuPart 各有一份独立数据。跟代码发现 put() 只写 LRU 分区，但 get() 里当 key 访问次数 ≥ transformThreshold（默认 2）时，会额外 new 一个 Node 写入 LFU 分区。两个分区各存各的，不是同一份。

修复：改为指针所有权转移。key 达到阈值时，不拷贝节点，而是从 LRU 的 map + 链表中摘除该 shared_ptr<Node>，原样插入 LFU 分区。一个 key 始终只有一份 Node，内存和 CPU 各降一半。

Bug 2：O(N) 频率链表扫描（P99 延迟毛刺）
现象：修复 Bug 1 后 ARC 吞吐量恢复不少，但 P99/P99.9 延迟仍然有毛刺，偶尔飙到几毫秒。

排查：perf top 看到热点在 std::list::remove。定位到 ArcLfuPart::updateNodeFrequency() 中的这一行：


auto& oldList = freqMap_[oldFreq];
oldList.remove(node);  // std::list::remove(value) — O(N)
list::remove(value) 按值匹配，需要遍历整个链表。Zipf 热点负载下，大量节点集中在低频段（freq=1~3），每个链表有几百上千个节点。每次 GET 都要扫一遍。

修复：引入 keyToIter_ 映射表（unordered_map<Key, list::iterator>），存储每个节点在链表中的精确位置。remove 从 O(N) 变为 erase(iterator) 的 O(1)：


auto it = keyToIter_[key];
oldList.erase(it);  // O(1)
Bug 3：数据竞争（多线程崩溃）
现象：多线程（--threads 4）跑一段时间后 segfault。

排查：加了 ThreadSanitizer 编译跑，报 increaseCapacity() 有 data race。检查代码发现 decreaseCapacity() 持有 mutex，但 increaseCapacity() 是裸写的，修改 capacity_ 时没加锁。

修复：给 increaseCapacity() 加上 std::lock_guard<std::mutex>，和 decreaseCapacity() 对称保护。

面试口述版（1 分钟讲完）
"ARC 刚开始跑出来的数据很差——吞吐量只有 LRU 的三分之一，多线程还会崩溃。我逐个排查定位了三个问题：

第一个是双份存储，key 在 LRU 和 LFU 分区里各存了一份独立副本，内存和 CPU 都翻倍了。改成节点所有权转移，一份 Node 在两个分区之间移动，不拷贝。

第二个是 O(N) 频率链表删除，每次更新节点频率时用 list::remove 扫描整个链表，Zipf 分布下低频链表很长，P99 延迟被拖垮。加了个迭代器映射表改成 O(1) 删除。

第三个是数据竞争，容量调节的 increaseCapacity 没加锁，多线程下跑一会儿就 segfault。补上锁就好了。

修复后 ARC 跟 LRU 性能差距缩到 ~10%，但命中率高出 3.7 个百分点，这个 trade-off 是符合预期的。"