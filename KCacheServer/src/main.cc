#include "CacheServer.h"
#include "Logger.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --port PORT          Listen port (default: 9999)\n"
              << "  --threads N          Worker thread count (default: 0 = single-threaded)\n"
              << "  --capacity N         Cache capacity (default: 10000)\n"
              << "  --policy NAME        Eviction policy: lru, lfu, arc, lruk (default: lru)\n"
              << "  --max-average N      LFU aging threshold (default: 1000000)\n"
              << "  --history-cap N      LRU-K history list capacity (default: 100)\n"
              << "  --history-k N        LRU-K access threshold (default: 2)\n"
              << "  --help               Show this help\n"
              << std::flush;
}

int main(int argc, char* argv[]) {
    int port = 9999;
    int threads = 0;
    int capacity = 10000;
    std::string policy = "lru";
    int maxAverage = 1000000;
    int historyCapacity = 100;
    int historyK = 2;

    // Parse CLI arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }

        auto nextVal = [&]() -> const char* {
            if (i + 1 < argc) return argv[++i];
            std::cerr << "Error: " << arg << " requires a value\n";
            std::exit(1);
        };

        if (arg == "--port")           port = std::atoi(nextVal());
        else if (arg == "--threads")   threads = std::atoi(nextVal());
        else if (arg == "--capacity")  capacity = std::atoi(nextVal());
        else if (arg == "--policy")    policy = nextVal();
        else if (arg == "--max-average")     maxAverage = std::atoi(nextVal());
        else if (arg == "--history-cap")     historyCapacity = std::atoi(nextVal());
        else if (arg == "--history-k")       historyK = std::atoi(nextVal());
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Validate
    if (capacity <= 0) {
        std::cerr << "Error: capacity must be positive\n";
        return 1;
    }

    LOG_INFO("KCacheServer starting:");
    LOG_INFO("  port       = %d", port);
    LOG_INFO("  threads    = %d", threads);
    LOG_INFO("  capacity   = %d", capacity);
    LOG_INFO("  policy     = %s", policy.c_str());
    LOG_INFO("  max-average= %d", maxAverage);

    auto cache = KCacheServer::createCache(policy, capacity,
                                           maxAverage, historyCapacity, historyK);
    if (!cache) {
        std::cerr << "Error: failed to create cache policy '" << policy << "'\n";
        return 1;
    }

    muduo::EventLoop loop;
    muduo::InetAddress addr(static_cast<uint16_t>(port));

    KCacheServer::CacheServer server(&loop, addr, "KCacheServer",
                                     std::move(cache), threads);
    server.start();

    LOG_INFO("KCacheServer listening on port %d", port);
    loop.loop();

    return 0;
}
