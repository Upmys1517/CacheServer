// 预处理指令：确保头文件在整个编译过程中只被包含一次
#pragma once

// 引入智能指针 std::unique_ptr / std::shared_ptr 等，用于自动内存管理
#include <memory>
// 引入 std::string 字符串类
#include <string>
// 引入 std::atomic 原子类型，用于多线程无锁计数
#include <atomic>
// 引入固定宽度整数类型：uint64_t 等
#include <cstdint>

// 引入 muduo 网络库的回调函数类型定义（如 TcpConnectionPtr）
#include "Callbacks.h"
// 引入 muduo 的 TCP 服务器封装类
#include "TcpServer.h"
// 引入 muduo 的网络地址类（IP + 端口）
#include "InetAddress.h"
// 引入 muduo 的事件循环类，是整个网络库的核心调度引擎
#include "EventLoop.h"




// 前向声明：KamaCache 命名空间中的泛型缓存策略接口
namespace KamaCache {
// 泛型缓存策略接口模板：Key 为键类型，Value 为值类型
// KICachePolicy 定义了 get/put/remove 等核心缓存操作
template <typename Key, typename Value>
class KICachePolicy;
}

// KCacheServer 命名空间：包含缓存服务器的所有实现
namespace KCacheServer {

// 前向声明协议解析类，避免头文件循环依赖
class Protocol;

// CacheServer 类：基于 muduo 网络库实现的 TCP 缓存服务器
// 通过协议解析接收命令，操作底层缓存策略完成 GET/SET/DEL 等操作
class CacheServer {
public:
    // 内部统计数据结构，记录服务器的运行指标
    struct Stats {
        uint64_t hits = 0;        // 缓存命中次数（GET 成功）
        uint64_t misses = 0;      // 缓存未命中次数（GET 失败）
        uint64_t sets = 0;        // 写入操作次数（SET 命令）
        uint64_t dels = 0;        // 删除操作次数（DEL 命令）
        uint64_t connections = 0; // 累计连接数
        uint64_t pings = 0;       // 心跳请求次数（PING 命令）
    };

    // 构造函数：初始化缓存服务器
    //   loop:      事件循环指针，驱动所有网络 I/O
    //   addr:      服务器监听的地址和端口
    //   name:      服务器名称标识
    //   cache:     缓存策略实例（通过工厂函数 createCache 创建），使用 unique_ptr 独占所有权
    //   threadNum: I/O 工作线程数量（0 表示单线程模式）
    CacheServer(EventLoop* loop,
                const InetAddress& addr,
                const std::string& name,
                std::unique_ptr<KamaCache::KICachePolicy<std::string, std::string>> cache,
                int threadNum = 0);

    // 析构函数：使用默认实现，由智能指针自动清理资源
    ~CacheServer();

    // 启动服务器：开始监听端口并接受连接
    void start();

    // 获取当前统计数据的快照（原子读取所有计数器）
    Stats getStats() const;

    // 获取服务器运行时长（秒），从 startTimeSec_ 算起到当前时间
    uint64_t uptimeSec() const;

private:
    // TCP 连接建立/断开时的回调函数
    //   conn: 指向该连接的共享指针
    void onConnection(const TcpConnectionPtr& conn);

    // 收到客户端消息时的回调函数
    //   conn: 指向该连接的共享指针
    //   buf:  接收数据的缓冲区
    //   time: 接收时间戳（此处未使用）
    void onMessage(const TcpConnectionPtr& conn,
                   Buffer* buf,
                   Timestamp time);

    // 核心命令处理函数：根据解析出的请求执行对应操作，返回序列化后的响应字符串
    std::string processCommand(const struct Request& req);

    // muduo TCP 服务器实例，封装了 accept/read/write 等底层操作
    TcpServer server_;

    // 缓存策略实例（多态），通过唯一指针独占所有权
    // 支持 LRU / LFU / ARC / LRU-K 等多种淘汰策略
    std::unique_ptr<KamaCache::KICachePolicy<std::string, std::string>> cache_;

    // ====== 统计计数器（原子类型，保证多线程读取安全）======

    std::atomic<uint64_t> hits_{0};        // 缓存命中计数
    std::atomic<uint64_t> misses_{0};      // 缓存未命中计数
    std::atomic<uint64_t> sets_{0};        // 写入操作计数
    std::atomic<uint64_t> dels_{0};        // 删除操作计数
    std::atomic<uint64_t> connections_{0}; // 连接计数
    std::atomic<uint64_t> pings_{0};       // 心跳计数
    uint64_t startTimeSec_;                // 服务器启动时刻（Unix 时间戳，秒）
};

// 工厂函数：根据策略名称字符串创建对应的缓存策略实例
//   policy:          缓存淘汰策略名称（"lru","lfu","arc","lruk"）
//   capacity:        缓存最大容量
//   maxAverage:      LFU 老化阈值（仅 LFU 使用，默认 1000000）
//   historyCapacity: LRU-K 的历史列表容量（仅 LRU-K 使用，默认 100）
//   k:               LRU-K 的访问次数阈值（仅 LRU-K 使用，默认 2）
// 返回值：指向缓存策略基类的唯一指针，如果策略名不匹配则默认返回 LRU
std::unique_ptr<KamaCache::KICachePolicy<std::string, std::string>>
createCache(const std::string& policy,
            int capacity,
            int maxAverage = 1000000,
            int historyCapacity = 100,
            int k = 2);

} // namespace KCacheServer
