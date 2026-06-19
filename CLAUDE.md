# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run Commands

### Build (in dependency order)

```bash
# 1. Build muduo-core shared library (C++11)
cd muduo-core && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# 2. Build KCacheServer + benchmark (C++17, depends on muduo-core .so)
cd KCacheServer && mkdir -p build && cd build && cmake .. && make -j$(nproc)
```

The KCacheServer CMakeLists.txt references muduo-core via an IMPORTED shared library at `../muduo-core/lib/libmuduo_core.so`. RPATH is embedded, so no `LD_LIBRARY_PATH` is needed.

### Run the cache policy test (header-only, no dependencies)

```bash
cd CacheSystem && mkdir -p build && cd build && cmake .. && make -j$(nproc) && ./main
```

### Run the server

```bash
./kcacheserver --port 9999 --policy lru --capacity 10000 --threads 4
```

Supported policies: `lru`, `lfu`, `arc`, `lruk`. LFU-specific flag: `--max-average` (aging threshold, default 1000000). LRU-K-specific flags: `--history-cap` (default 100), `--history-k` (default 2).

### Manual smoke test

```bash
echo -e "PING\r\nSET foo bar\r\nGET foo\r\nSTATS\r\nQUIT\r\n" | nc localhost 9999
```

### Run benchmark

```bash
./benchmark --host 127.0.0.1 --port 9999 --connections 10 --requests 100000 \
            --keyspace 5000 --read-ratio 0.8
```

Benchmark CLI flags: `--host`, `--port`, `--connections` (concurrent client threads), `--requests` (per connection), `--keyspace` (number of distinct keys), `--read-ratio` (0.0–1.0, GET vs SET), `--value-size` (default 128 bytes), `--alpha` (Zipf skew, 0 = uniform).

## Architecture

Three-layer design, each layer independently buildable:

```
KCacheServer (application)  →  TCP server + text protocol + command dispatch
    ↓ depends on
CacheSystem (policy)        →  header-only templates: LRU, LFU, ARC, LRU-K
    ↓ independent from
muduo-core (network)        →  Reactor / epoll / One Loop Per Thread / shared .so
```

### muduo-core — Reactor network library

A simplified C++11 reimplementation of the muduo networking library. Core classes:
- **`EventLoop`** — main loop calling `epoll_wait`, dispatching to `Channel`s. Each thread has its own `EventLoop` ("One Loop Per Thread").
- **`EPollPoller`** — wraps `epoll_create`/`epoll_ctl`/`epoll_wait`.
- **`Channel`** — binds an fd + interested events (EPOLLIN/EPOLLOUT) + callbacks. Non-owning.
- **`TcpServer`** — owns an `Acceptor`, an `EventLoopThreadPool`, and all `TcpConnection` shared_ptrs.
- **`TcpConnection`** — per-connection state machine with input/output `Buffer`s. Callbacks: `onConnection`, `onMessage`, `onWriteComplete`.
- **`Buffer`** — prependable/readable/writable buffer, handles TCP framing.
- **`Logger.h`** — singleton logger with `LOG_INFO`/`LOG_ERROR`/`LOG_FATAL`/`LOG_DEBUG` macros.

### CacheSystem — header-only cache policies

All in `KamaCache` namespace. Base interface at [CachePolicy.h](CacheSystem/CachePolicy.h):
```cpp
template <typename Key, typename Value>
class KICachePolicy {
    virtual void put(Key key, Value value) = 0;
    virtual bool get(Key key, Value& value) = 0;
    virtual Value get(Key key) = 0;
    virtual void remove(Key key) = 0;
};
```

Implementations:
- **`KLruCache`** — `std::list` + `std::unordered_map`, O(1) get/put.
- **`KLruKCache`** — inherits `KLruCache`; adds a history list tracking K accesses before promotion to main cache.
- **`KHashLruCaches`** — sharded LRU (N independent LRU instances, hashed by key), reduces lock contention.
- **`KLfuCache`** — frequency-based with aging/decay via `maxAverageNum` threshold. O(1) get, O(N) put eviction (scans freq list from minFreq upward).
- **`KHashLfuCache`** — sharded LFU.
- **`KArcCache`** (4 files in `ArcCache/`) — Adaptive Replacement Cache: balances LRU and LFU partitions, each with a ghost cache. Uses mutex internally. See [bug.md](KCacheServer/bug.md) for 3 bugs fixed during development (double storage, O(N) freq list removal, data race on `decreaseCapacity`).

### KCacheServer — TCP cache server

Entry point: [main.cc](KCacheServer/src/main.cc). Parses CLI args → `createCache()` factory → constructs `CacheServer` → `loop.loop()`.

**`CacheServer`** ([CacheServer.h](KCacheServer/src/CacheServer.h)): owns a `TcpServer` and a `KICachePolicy<string, string>`. Registers two muduo callbacks:
- `onConnection` — increments atomic connection counter.
- `onMessage` — calls `Protocol::parse()` to extract a command from the buffer, then `processCommand()` which does a switch on `Command` enum (GET/SET/DEL/PING/STATS/QUIT) and calls the corresponding cache method.

**`Protocol`** ([Protocol.h](KCacheServer/src/Protocol.h)): text-based, `\r\n`-delimited (also tolerates bare `\n`). Commands are case-insensitive. Response format: `VALUE <v>\r\n`, `NIL\r\n`, `OK\r\n`, `PONG\r\n`, `ERR <msg>\r\n`, or multi-line key:value for STATS.

**`createCache()`** factory in [CacheServer.cc](KCacheServer/src/CacheServer.cc): maps policy name string → concrete cache type, wiring LFU aging and LRU-K history parameters.

**`benchmark.cc`**: standalone multi-threaded client using raw POSIX sockets (epoll + non-blocking connect). Does NOT use muduo. Measures throughput, hit rate, and latency percentiles (P50/P90/P99/P99.9).

## Key Design Details

- **No test framework** — testing is via `CacheSystem/testAllCachePolicy.cpp` (hit-rate comparison across policies and workload patterns), manual `nc` commands, and the `benchmark` binary.
- **Thread safety**: `CacheServer` uses `std::atomic` for stats counters. `KArcCache` uses `std::mutex`. `KHashLruCaches`/`KHashLfuCache` use sharding to reduce contention. LRU and LFU are NOT internally synchronized — the server relies on muduo's thread-per-loop model for isolation.
- **CacheSystem is truly header-only** — no `.cc` files, no library to link. The CMakeLists.txt there only builds the test binary.
- **CMake version**: 3.10 minimum. muduo-core uses C++11, CacheSystem and KCacheServer use C++17.
