# 构建与测试指南

## 1. 编译 muduo-core

```bash
cd muduo-core
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

## 2. 编译 KCacheServer

```bash
cd KCacheServer
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

## 3. 启动服务器

```bash
# LRU 策略，4 线程，容量 10000
./kcacheserver --port 9999 --policy lru --capacity 10000 --threads 4
```

## 4. 手动测试

```bash
echo -e "PING\r\nSET foo bar\r\nGET foo\r\nSTATS\r\nQUIT\r\n" | nc localhost 9999
```

## 5. 基准测试

```bash
./benchmark --host 127.0.0.1 --port 9999 --connections 10 --requests 100000 --keyspace 5000 --read-ratio 0.8
```

## 6. 对比不同策略

分别启动不同策略的服务器，用 benchmark 压测，结束后 kill 换下一个策略：

```bash
./kcacheserver --port 9999 --policy lru --capacity 10000 &
./benchmark --port 9999 ...  # 测试 LRU
# kill 服务器，换下一个策略

./kcacheserver --port 9999 --policy lfu --capacity 10000 &
./benchmark --port 9999 ...  # 测试 LFU
# kill 服务器，换下一个策略

./kcacheserver --port 9999 --policy arc --capacity 10000 &
./benchmark --port 9999 ...  # 测试 ARC
# kill 服务器，换下一个策略

./kcacheserver --port 9999 --policy lruk --capacity 10000 &
./benchmark --port 9999 ...  # 测试 LRU-K
```
