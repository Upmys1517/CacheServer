#include "CacheServer.h"
#include "Protocol.h"

#include "Logger.h"
#include "TcpConnection.h"
#include "Buffer.h"

#include "CachePolicy.h"
#include "LruCache.h"
#include "LfuCache.h"
#include "ArcCache/ArcCache.h"

#include <chrono>

namespace KCacheServer {

CacheServer::CacheServer(
    muduo::EventLoop* loop,
    const muduo::InetAddress& addr,
    const std::string& name,
    std::unique_ptr<KamaCache::KICachePolicy<std::string, std::string>> cache,
    int threadNum)
    : server_(loop, addr, name)
    , cache_(std::move(cache))
{
    server_.setConnectionCallback(
        [this](const muduo::TcpConnectionPtr& conn) { onConnection(conn); });
    server_.setMessageCallback(
        [this](const muduo::TcpConnectionPtr& conn, muduo::Buffer* buf, muduo::Timestamp time) {
            onMessage(conn, buf, time);
        });
    server_.setThreadNum(threadNum);

    startTimeSec_ = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

CacheServer::~CacheServer() = default;

void CacheServer::start() {
    server_.start();
}

CacheServer::Stats CacheServer::getStats() const {
    Stats s;
    s.hits = hits_.load();
    s.misses = misses_.load();
    s.sets = sets_.load();
    s.dels = dels_.load();
    s.connections = connections_.load();
    s.pings = pings_.load();
    return s;
}

uint64_t CacheServer::uptimeSec() const {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return static_cast<uint64_t>(now - startTimeSec_);
}

void CacheServer::onConnection(const muduo::TcpConnectionPtr& conn) {
    if (conn->connected()) {
        connections_++;
        LOG_INFO("Connection UP : %s [%s]",
                 conn->peerAddress().toIpPort().c_str(),
                 conn->name().c_str());
    } else {
        LOG_INFO("Connection DOWN : %s [%s]",
                 conn->peerAddress().toIpPort().c_str(),
                 conn->name().c_str());
    }
}

void CacheServer::onMessage(const muduo::TcpConnectionPtr& conn,
                            muduo::Buffer* buf,
                            muduo::Timestamp /*time*/) {
    Request req;
    while (Protocol::parse(buf, &req)) {
        if (req.cmd == Command::QUIT) {
            conn->send("BYE\r\n");
            conn->shutdown();
            return;
        }

        std::string response = processCommand(req);
        conn->send(response);
    }
}

std::string CacheServer::processCommand(const Request& req) {
    switch (req.cmd) {
    case Command::PING:
        pings_++;
        return Protocol::encodePong();

    case Command::GET: {
        if (req.key.empty()) {
            return Protocol::encodeError("GET requires a key");
        }
        std::string value;
        if (cache_->get(req.key, value)) {
            hits_++;
            return Protocol::encodeGetResponse(value);
        } else {
            misses_++;
            return Protocol::encodeNil();
        }
    }

    case Command::SET: {
        if (req.key.empty()) {
            return Protocol::encodeError("SET requires a key");
        }
        cache_->put(req.key, req.value);
        sets_++;
        return Protocol::encodeOk();
    }

    case Command::DEL: {
        if (req.key.empty()) {
            return Protocol::encodeError("DEL requires a key");
        }
        // Check if key exists, then overwrite with sentinel
        std::string dummy;
        if (cache_->get(req.key, dummy)) {
            cache_->put(req.key, "");
            dels_++;
            return Protocol::encodeOk();
        } else {
            return Protocol::encodeNil();
        }
    }

    case Command::STATS: {
        Stats s = getStats();
        return Protocol::encodeStats(
            s.hits, s.misses, s.sets, s.dels,
            s.connections, s.pings, uptimeSec());
    }

    default:
        return Protocol::encodeError("unknown command");
    }
}

// Factory function
std::unique_ptr<KamaCache::KICachePolicy<std::string, std::string>>
createCache(const std::string& policy,
            int capacity,
            int maxAverage,
            int historyCapacity,
            int k)
{
    using CachePtr = std::unique_ptr<KamaCache::KICachePolicy<std::string, std::string>>;

    if (policy == "lru") {
        return CachePtr(new KamaCache::KLruCache<std::string, std::string>(capacity));
    }
    if (policy == "lfu") {
        return CachePtr(new KamaCache::KLfuCache<std::string, std::string>(capacity, maxAverage));
    }
    if (policy == "arc") {
        return CachePtr(new KamaCache::KArcCache<std::string, std::string>(capacity));
    }
    if (policy == "lruk") {
        return CachePtr(new KamaCache::KLruKCache<std::string, std::string>(capacity, historyCapacity, k));
    }
    // Default: LRU
    return CachePtr(new KamaCache::KLruCache<std::string, std::string>(capacity));
}

} // namespace KCacheServer
