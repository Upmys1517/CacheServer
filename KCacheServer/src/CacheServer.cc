// 包含自己的头文件声明
#include "CacheServer.h"
// 包含协议解析类的完整定义
#include "Protocol.h"

// 引入 muduo 日志宏（LOG_INFO 等）
#include "Logger.h"
// 引入 muduo TCP 连接类，提供 peerAddress/name/send/shutdown 等方法
#include "TcpConnection.h"
// 引入 muduo 缓冲区类，存储接收到的原始数据
#include "Buffer.h"

// 引入泛型缓存策略基类接口
#include "CachePolicy.h"
// 引入 LRU（最近最少使用）缓存策略实现
#include "LruCache.h"
// 引入 LFU（最不经常使用）缓存策略实现
#include "LfuCache.h"
// 引入 ARC（自适应替换缓存）策略实现
#include "ArcCache/ArcCache.h"
// 引入 chrono 时间库，用于获取系统时间和计算运行时长
#include <chrono>

// KCacheServer 命名空间实现
namespace KCacheServer {

// CacheServer 构造函数：初始化服务器和缓存
CacheServer::CacheServer(
    EventLoop* loop,                                              // 事件循环指针
    const InetAddress& addr,                                      // 监听地址（IP + 端口）
    const std::string& name,                                      // 服务器名称
    std::unique_ptr<KamaCache::KICachePolicy<std::string, std::string>> cache, // 缓存策略（所有权转移）
    int threadNum)                                                // I/O 线程数
    // 初始化列表：构造 TcpServer 并转移缓存所有权
    : server_(loop, addr, name)
    , cache_(std::move(cache))
{
    // 设置连接建立/断开的回调函数，通过 lambda 捕获 this 调用 onConnection
    server_.setConnectionCallback(
        [this](const TcpConnectionPtr& conn) { onConnection(conn); });

    // 设置消息到达的回调函数，通过 lambda 捕获 this 调用 onMessage
    server_.setMessageCallback(
        [this](const TcpConnectionPtr& conn, Buffer* buf, Timestamp time) {
            onMessage(conn, buf, time);
        });

    // 设置 I/O 工作线程数量（0 表示主线程处理所有 I/O）
    server_.setThreadNum(threadNum);

    // 记录服务器启动时刻的 Unix 时间戳（秒），用于计算 uptime
    startTimeSec_ = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// 析构函数：使用默认实现，编译器自动生成
// server_ 和 cache_ 由各自的析构函数自动清理
CacheServer::~CacheServer() = default;

// 启动服务器：开始监听端口，接受客户端连接
void CacheServer::start() {
    server_.start();
}

// 获取统计信息快照：原子读取各计数器，填充到 Stats 结构体返回
CacheServer::Stats CacheServer::getStats() const {
    Stats s;
    s.hits = hits_.load();              // 原子读取缓存命中数
    s.misses = misses_.load();          // 原子读取缓存未命中数
    s.sets = sets_.load();              // 原子读取写入操作数
    s.dels = dels_.load();              // 原子读取删除操作数
    s.connections = connections_.load();// 原子读取连接数
    s.pings = pings_.load();           // 原子读取心跳数
    return s;
}

// 计算服务器运行时长（秒）：当前时间减去启动时间
uint64_t CacheServer::uptimeSec() const {
    // 获取当前系统时间戳（秒）
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // 返回差值，即服务器已经运行的秒数
    return static_cast<uint64_t>(now - startTimeSec_);
}

// 连接状态变化回调：TCP 连接建立或断开时被调用
void CacheServer::onConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        // 连接建立：递增连接计数器
        connections_++;
        // 打印日志：记录对端地址和连接名称
        LOG_INFO("Connection UP : %s [%s]",
                 conn->peerAddress().toIpPort().c_str(),
                 conn->name().c_str());
    } else {
        // 连接断开：打印日志记录
        LOG_INFO("Connection DOWN : %s [%s]",
                 conn->peerAddress().toIpPort().c_str(),
                 conn->name().c_str());
    }
}

// 消息到达回调：客户端发送数据时被调用
void CacheServer::onMessage(const TcpConnectionPtr& conn,
                            Buffer* buf,
                            Timestamp /*time*/) {  // time 参数未使用，标注名字忽略编译器警告
    Request req;
    // 循环解析缓冲区中的所有完整命令（可能一次收到多条）
    while (Protocol::parse(buf, &req)) {
        // 如果收到 QUIT 命令，发送 BYE 后主动关闭连接
        if (req.cmd == Command::QUIT) {
            conn->send("BYE\r\n");
            conn->shutdown();  // 优雅关闭连接（发送 FIN 包）
            return;
        }

        // 处理其他命令，获取响应字符串
        std::string response = processCommand(req);
        // 通过 TCP 连接发送响应给客户端
        conn->send(response);
    }
}

// 核心命令分发处理器：根据请求中的命令类型执行相应操作
std::string CacheServer::processCommand(const Request& req) {
    switch (req.cmd) {
    case Command::PING:
        // 心跳命令：递增计数并返回 PONG
        pings_++;
        return Protocol::encodePong();

    case Command::GET: {
        // GET 命令需要 key 参数
        if (req.key.empty()) {
            return Protocol::encodeError("GET requires a key");
        }
        std::string value;
        // 从缓存中查找 key 对应的值
        if (cache_->get(req.key, value)) {
            // 命中：递增 hits 计数，返回 VALUE 响应
            hits_++;
            return Protocol::encodeGetResponse(value);
        } else {
            // 未命中：递增 misses 计数，返回 NIL 响应
            misses_++;
            return Protocol::encodeNil();
        }
    }

    case Command::SET: {
        // SET 命令需要 key 参数
        if (req.key.empty()) {
            return Protocol::encodeError("SET requires a key");
        }
        // 将 key-value 对写入缓存
        cache_->put(req.key, req.value);
        sets_++;
        return Protocol::encodeOk();
    }

    case Command::DEL: {
        // DEL 命令需要 key 参数
        if (req.key.empty()) {
            return Protocol::encodeError("DEL requires a key");
        }
        // 从缓存中移除指定 key
        cache_->remove(req.key);
        dels_++;
        return Protocol::encodeOk();
    }

    case Command::STATS: {
        // 获取服务器运行统计快照
        Stats s = getStats();
        // 编码为多行统计信息字符串返回
        return Protocol::encodeStats(
            s.hits, s.misses, s.sets, s.dels,
            s.connections, s.pings, uptimeSec());
    }

    default:
        // 未知命令：返回错误信息
        return Protocol::encodeError("unknown command");
    }
}

// ====== 工厂函数：根据策略名称创建对应的缓存实例 ======

std::unique_ptr<KamaCache::KICachePolicy<std::string, std::string>>
createCache(const std::string& policy,   // 缓存策略名称（不区分大小写）
            int capacity,                // 缓存最大容量
            int maxAverage,              // LFU 老化阈值参数
            int historyCapacity,         // LRU-K 历史队列容量
            int k)                       // LRU-K 访问次数阈值
{
    // 类型别名：简化智能指针类型名称
    using CachePtr = std::unique_ptr<KamaCache::KICachePolicy<std::string, std::string>>;

    // 根据策略名称字符串创建对应的缓存实例
    if (policy == "lru") {
        // LRU：最近最少使用，仅需容量参数
        return CachePtr(new KamaCache::KLruCache<std::string, std::string>(capacity));
    }
    if (policy == "lfu") {
        // LFU：最不经常使用，额外需要 maxAverage 老化参数防止历史频率过时
        return CachePtr(new KamaCache::KLfuCache<std::string, std::string>(capacity, maxAverage));
    }
    if (policy == "arc") {
        // ARC：自适应替换缓存，平衡 LRU 和 LFU 的优点
        return CachePtr(new KamaCache::KArcCache<std::string, std::string>(capacity));
    }
    if (policy == "lruk") {
        // LRU-K：基于访问次数的 LRU 变体，额外需要历史队列容量和访问阈值 k
        return CachePtr(new KamaCache::KLruKCache<std::string, std::string>(capacity, historyCapacity, k));
    }
    // 默认策略：如果名称不匹配任何已知策略，降级为 LRU
    return CachePtr(new KamaCache::KLruCache<std::string, std::string>(capacity));
}

} // namespace KCacheServer
