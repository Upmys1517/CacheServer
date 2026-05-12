# 1. 先编译 muduo_core
cd d:/vscode/muduo_core
mkdir -p build && cd build
cmake .. && make -j$(nproc)

# 2. 编译 KCacheServer
cd d:/vscode/KCacheServer
mkdir -p build && cd build
cmake .. && make -j$(nproc)

# 3. 启动服务器（LRU 策略，4 线程，容量 10000）
./kcacheserver --port 9999 --policy lru --capacity 10000 --threads 4

# 4. 手动测试
echo -e "PING\r\nSET foo bar\r\nGET foo\r\nSTATS\r\nQUIT\r\n" | nc localhost 9999

# 5. 基准测试
./benchmark --host 127.0.0.1 --port 9999 --connections 10 --requests 100000 --keyspace 5000 --read-ratio 0.8

# 6. 对比不同策略（按顺序测试 lru / lfu / arc / lruk）
  # 分别启动不同策略的服务器，用 benchmark 对比性能
  ./kcacheserver --port 9999 --policy lru --capacity 10000 &
  ./benchmark --port 9999 ...
  # 结束后 kill，换下一个策略