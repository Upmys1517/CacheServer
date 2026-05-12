#pragma once

#include <memory>
#include <string>
#include <atomic>
#include <cstdint>

#include "Callbacks.h"
#include "TcpServer.h"
#include "InetAddress.h"
#include "EventLoop.h"




namespace KamaCache {
template <typename Key, typename Value>
class KICachePolicy;
}

namespace KCacheServer {

class Protocol;

class CacheServer {
public:
    struct Stats {
        uint64_t hits = 0;
        uint64_t misses = 0;
        uint64_t sets = 0;
        uint64_t dels = 0;
        uint64_t connections = 0;
        uint64_t pings = 0;
    };

    CacheServer(EventLoop* loop,
                const InetAddress& addr,
                const std::string& name,
                std::unique_ptr<KamaCache::KICachePolicy<std::string, std::string>> cache,
                int threadNum = 0);

    ~CacheServer();

    void start();
    Stats getStats() const;
    uint64_t uptimeSec() const;

private:
    void onConnection(const TcpConnectionPtr& conn);
    void onMessage(const TcpConnectionPtr& conn,
                   Buffer* buf,
                   Timestamp time);

    std::string processCommand(const struct Request& req);

    TcpServer server_;
    std::unique_ptr<KamaCache::KICachePolicy<std::string, std::string>> cache_;

    // Statistics
    std::atomic<uint64_t> hits_{0};
    std::atomic<uint64_t> misses_{0};
    std::atomic<uint64_t> sets_{0};
    std::atomic<uint64_t> dels_{0};
    std::atomic<uint64_t> connections_{0};
    std::atomic<uint64_t> pings_{0};
    uint64_t startTimeSec_;
};

// Factory function: create a cache policy by name
std::unique_ptr<KamaCache::KICachePolicy<std::string, std::string>>
createCache(const std::string& policy,
            int capacity,
            int maxAverage = 1000000,
            int historyCapacity = 100,
            int k = 2);

} // namespace KCacheServer
