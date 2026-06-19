// 包含缓存服务器的类定义
#include "CacheServer.h"
// 包含泛型缓存策略基类接口
#include "CachePolicy.h"
// 引入 muduo 日志宏（LOG_INFO 等）
#include "Logger.h"

// 标准库头文件，分别用于 atoi、字符串操作、控制台 I/O
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

// 打印命令行帮助信息，列出所有可用参数及其默认值
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

// 程序入口：解析命令行参数 → 创建缓存策略 → 配置事件循环 → 启动服务器
int main(int argc, char* argv[]) {
    // 默认配置参数（可通过命令行选项覆盖）
    int port = 9999;                    // 监听端口
    int threads = 0;                    // I/O 工作线程数（0 = 主线程处理所有 I/O）
    int capacity = 10000;               // 缓存最大容量
    std::string policy = "lru";         // 淘汰策略名称
    int maxAverage = 1000000;           // LFU 老化阈值
    int historyCapacity = 100;          // LRU-K 历史队列容量
    int historyK = 2;                   // LRU-K 访问次数阈值

    // 解析命令行参数（从 argv[1] 开始，argv[0] 是程序名）
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        // 帮助选项：打印用法后正常退出
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }

        // 辅助 lambda：获取下一个参数值，如果不存在则报错退出
        auto nextVal = [&]() -> const char* {
            if (i + 1 < argc) return argv[++i];
            std::cerr << "Error: " << arg << " requires a value\n";
            std::exit(1);
        };

        // 逐个匹配选项，提取对应的参数值
        if (arg == "--port")           port = std::atoi(nextVal());
        else if (arg == "--threads")   threads = std::atoi(nextVal());
        else if (arg == "--capacity")  capacity = std::atoi(nextVal());
        else if (arg == "--policy")    policy = nextVal();
        else if (arg == "--max-average")     maxAverage = std::atoi(nextVal());
        else if (arg == "--history-cap")     historyCapacity = std::atoi(nextVal());
        else if (arg == "--history-k")       historyK = std::atoi(nextVal());
        else {
            // 未知参数：报错并打印帮助信息
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // 校验：容量必须为正数
    if (capacity <= 0) {
        std::cerr << "Error: capacity must be positive\n";
        return 1;
    }

    // 打印启动配置信息（调试/监控用途）
    LOG_INFO("KCacheServer starting:");
    LOG_INFO("  port       = %d", port);
    LOG_INFO("  threads    = %d", threads);
    LOG_INFO("  capacity   = %d", capacity);
    LOG_INFO("  policy     = %s", policy.c_str());
    LOG_INFO("  max-average= %d", maxAverage);

    // 通过工厂函数创建指定策略的缓存实例
    auto cache = KCacheServer::createCache(policy, capacity,
                                           maxAverage, historyCapacity, historyK);
    if (!cache) {
        std::cerr << "Error: failed to create cache policy '" << policy << "'\n";
        return 1;
    }

    EventLoop loop;                                     // 创建事件循环（muduo 核心调度器）
    InetAddress addr(static_cast<uint16_t>(port), "0.0.0.0");     // 构建监听地址（所有网卡 + 指定端口）

    // 创建缓存服务器实例，将缓存所有权转移给服务器
    KCacheServer::CacheServer server(&loop, addr, "KCacheServer",
                                     std::move(cache), threads);
    server.start();  // 开始监听端口

    LOG_INFO("KCacheServer listening on port %d", port);
    loop.loop();     // 进入事件循环（阻塞直到服务器停止）

    return 0;
}
