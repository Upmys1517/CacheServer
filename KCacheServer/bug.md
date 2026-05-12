  Bug 1：双份存储（性能主因）
  
  ArcCache::put() 永远只向 lruPart 写入，ArcCache::get() 命中且访问次数 ≥ transformThreshold（默认 2）时，额外向 lfuPart 写入一份。两份是独立的
  shared_ptr<Node>，不是同一个对象。
  
  访问 2 次后的 key 状态：
    lruPart_[mainCache_]["key:0000001"]  →  Node_A
    lfuPart_[mainCache_]["key:0000001"]  →  Node_B  ← 完全独立的副本

  你设 capacity=5000，实际每个 part 都有自己的 capacity=5000。一个 key 同时占两个 part，有效去重容量只有 ~5000，但内存和 CPU 开销是双倍的。

  Bug 2：O(N) 频率链表扫描（延迟主因）
  
  ArcLfuPart::updateNodeFrequency() 中：
  
  // ArcLfuPart.h:157  每次 GET 命中都要执行
  auto& oldList = freqMap_[oldFreq];
  oldList.remove(node);  // std::list::remove — O(N) 线性扫描！

  std::list::remove(value) 按值匹配需要遍历整个链表。Zipf 分布下，大量节点集中在低频（freq=1~3），每个链表可能有数百到上千个节点，每次 GET 都扫一遍。

  Bug 3：数据竞争（可能导致崩溃）
  
  ArcLruPart::increaseCapacity() 和 ArcLfuPart::increaseCapacity() 修改 capacity_ 时不持锁，而 decreaseCapacity() 是持锁的。多线程并发下这是未定义行为。
  
  Bug 4：断开连接的真正原因
  
  上述 1+2 导致 ARC 处理速度远低于 LRU/LFU。10 个并发连接 × 50000 请求的高压下，muduo 的发送缓冲区被撑满，触发高水位回调关闭了连接（或者 Bug 3 的 race 直接导致
  segfault）。

  ---
  主要改三点：
  1. 让 LRU/LFU 共享同一个 Node，不再双份存储
  2. freqMap_ 的 list 改用 std::list<iterator> 模式，或直接用 std::map<freq, std::list<Key>> 避免 O(N) 的 remove
  3. increaseCapacity() 加锁



    ArcLruPart.h — 3 处改动：
  - increaseCapacity() 加锁（修复数据竞争）
  - get() 新增重载，当 shouldTransform 且传入 outNode 时，直接从 LRU 的 map+链表 中移除节点并传出（为移动而非拷贝铺路）
  - 新增 contain() 和 updateIfExists() 方法

  ArcLfuPart.h — 4 处改动：
  - increaseCapacity()、contain()、checkGhost() 加锁（修复数据竞争）
  - 新增 KeyToIter 映射表 + keyToIter_ 成员
  - updateNodeFrequency() 用 erase(iterator) 替换 list::remove(value)：O(N) → O(1)
  - 新增 addExistingNode()（接收从 LRU 移过来的节点）和 updateIfExists() 方法

  ArcCache.h — put() / get() 重写：
  修复前： key 同时在 lruPart 和 lfuPart 各存一份独立 Node（双倍内存，双倍 CPU）
  修复后： key 只存一份 Node，在 lruPart ↔ lfuPart 之间移动（转移所有权，零拷贝）

  ARC 比 LRU 慢约 10% 是符合预期的（维护两套结构的开销），换来的是 Zipf 热点负载下命中率 4+ 个百分点的优势。