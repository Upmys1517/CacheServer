// POSIX socket API 头文件：地址转换、地址结构、socket 函数、close/read
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// 标准库：排序/最小元素、原子计数器、高精度计时器、数值转换、字符串操作、格式化输出
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// 类型别名：使用高精度时钟测量延迟和吞吐量
using Clock = std::chrono::high_resolution_clock;

// 基准测试配置参数结构体
struct BenchmarkConfig {
    std::string host = "127.0.0.1"; // 目标服务器地址
    int port = 9999;                 // 目标服务器端口
    int connections = 10;            // 并发连接数（线程数）
    int requestsPerConn = 10000;     // 每连接的请求数
    int keySpace = 1000;             // 唯一 key 的数量（用于 Zipf 式均匀分布）
    double readRatio = 0.8;          // GET 请求占比（其余为 SET）
    int valueSize = 64;              // SET 生成的 value 长度（字节）
};

// 基准测试结果结构体
struct BenchmarkResult {
    uint64_t totalOps = 0;                // 总操作数（GET + SET）
    uint64_t totalHits = 0;               // GET 命中次数
    uint64_t totalMisses = 0;             // GET 未命中次数
    double durationSec = 0;               // 测试运行时长（秒）
    double opsPerSec = 0;                 // 吞吐量（每秒操作数）
    std::vector<double> latenciesUs;      // 每次请求的延迟（微秒）
};

// 生成指定长度的随机字母数字字符串，作为 SET 的 value
static std::string randomValue(int len, std::mt19937& rng) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::uniform_int_distribution<int> dist(0, sizeof(chars) - 2);
    std::string s(len, '\0');
    for (int i = 0; i < len; ++i) s[i] = chars[dist(rng)];
    return s;
}

// 生成格式化的 key 字符串，如 "key:0000042"（固定宽度、零填充）
static std::string makeKey(int i) {
    std::ostringstream oss;
    oss << "key:" << std::setw(7) << std::setfill('0') << i;
    return oss.str();
}

// 从 socket 读取一行响应（以 \r\n 结束），返回去掉 \r\n 的内容
// 使用内部缓冲区 recvBuf 缓存剩余数据，避免粘包问题
static std::string readResponse(int fd, std::string& recvBuf) {
    char tmp[4096];
    while (true) {
        // 先在已有缓冲区中搜索 \r\n
        size_t pos = recvBuf.find("\r\n");
        if (pos != std::string::npos) {
            std::string line = recvBuf.substr(0, pos);
            recvBuf.erase(0, pos + 2);  // 从缓冲区中消耗此行
            return line;
        }
        // 无完整行时从 socket 继续读取数据
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) return "";  // 连接关闭或出错
        recvBuf.append(tmp, static_cast<size_t>(n));
    }
}

// 单个连接的工作函数：在一个线程中执行一批请求，记录延迟和命中率
static void benchmarkWorker(int connId,
                            const BenchmarkConfig& cfg,
                            std::atomic<uint64_t>& totalOps,
                            std::atomic<uint64_t>& totalHits,
                            std::atomic<uint64_t>& totalMisses,
                            std::vector<double>& latencies,
                            std::mutex& latMutex)
{
    // 创建 TCP socket
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "Worker " << connId << ": socket() failed\n";
        return;
    }

    // 构建目标服务器地址结构
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(cfg.port));
    if (::inet_pton(AF_INET, cfg.host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "Worker " << connId << ": invalid address\n";
        ::close(fd);
        return;
    }

    // 连接到服务器
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Worker " << connId << ": connect() failed\n";
        ::close(fd);
        return;
    }

    // 初始化随机数生成器（种子加上 connId 避免各线程同序列）
    std::mt19937 rng(std::random_device{}() + connId);
    std::uniform_int_distribution<int> keyDist(0, cfg.keySpace - 1);
    std::uniform_real_distribution<double> opDist(0.0, 1.0);

    std::string recvBuf;           // 响应接收缓冲区（复用，减少分配）
    std::vector<double> localLatencies;
    localLatencies.reserve(static_cast<size_t>(cfg.requestsPerConn));

    uint64_t localHits = 0;
    uint64_t localMisses = 0;

    // 执行请求循环
    for (int i = 0; i < cfg.requestsPerConn; ++i) {
        std::string key = makeKey(keyDist(rng));
        std::string request;

        // 根据 readRatio 决定本次执行 GET 还是 SET
        if (opDist(rng) < cfg.readRatio) {
            request = "GET " + key + "\r\n";
        } else {
            std::string value = randomValue(cfg.valueSize, rng);
            request = "SET " + key + " " + value + "\r\n";
        }

        auto t1 = Clock::now();  // 记录请求发送前时间

        // 发送请求（循环确保全部数据发送完毕）
        size_t sent = 0;
        while (sent < request.size()) {
            ssize_t n = ::send(fd, request.data() + sent, request.size() - sent, 0);
            if (n <= 0) {
                std::cerr << "Worker " << connId << ": send() failed\n";
                ::close(fd);
                return;
            }
            sent += static_cast<size_t>(n);
        }

        // 读取响应
        std::string line = readResponse(fd, recvBuf);
        if (line.empty()) {
            std::cerr << "Worker " << connId << ": connection closed by server\n";
            ::close(fd);
            return;
        }

        auto t2 = Clock::now();  // 记录响应接收后时间
        // 计算本条请求的延迟（微秒）
        double us = static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count());
        localLatencies.push_back(us);

        // 统计 GET 请求的命中/未命中（以 "VALUE" 开头的响应表示命中）
        if (request[0] == 'G') {
            if (line.rfind("VALUE", 0) == 0) {
                localHits++;
            } else {
                localMisses++;
            }
        }
    }

    // 优雅关闭连接
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);

    // 将本地统计合并到全局结果（原子操作用于计数，互斥锁用于延迟数组）
    totalOps += static_cast<uint64_t>(cfg.requestsPerConn);
    totalHits += localHits;
    totalMisses += localMisses;

    {
        std::lock_guard<std::mutex> lock(latMutex);
        latencies.insert(latencies.end(), localLatencies.begin(), localLatencies.end());
    }
}

// 运行基准测试：创建多个连接线程并发执行请求，收集吞吐量和延迟数据
static BenchmarkResult runBenchmark(const BenchmarkConfig& cfg) {
    std::atomic<uint64_t> totalOps{0};
    std::atomic<uint64_t> totalHits{0};
    std::atomic<uint64_t> totalMisses{0};
    std::vector<double> latencies;
    std::mutex latMutex;

    // 打印测试参数
    std::cout << "Starting benchmark:\n"
              << "  target:    " << cfg.host << ":" << cfg.port << "\n"
              << "  connections: " << cfg.connections << "\n"
              << "  requests/conn: " << cfg.requestsPerConn << "\n"
              << "  total requests: " << (cfg.connections * cfg.requestsPerConn) << "\n"
              << "  key space: " << cfg.keySpace << "\n"
              << "  read ratio: " << (cfg.readRatio * 100) << "%\n"
              << "  value size: " << cfg.valueSize << " bytes\n"
              << "Running..." << std::endl;

    auto t1 = Clock::now();  // 记录测试开始时间

    // 创建与连接数等量的工作线程
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(cfg.connections));
    for (int i = 0; i < cfg.connections; ++i) {
        workers.emplace_back(benchmarkWorker, i, std::ref(cfg),
                             std::ref(totalOps), std::ref(totalHits),
                             std::ref(totalMisses),
                             std::ref(latencies), std::ref(latMutex));
    }

    // 等待所有工作线程完成
    for (auto& t : workers) {
        t.join();
    }

    auto t2 = Clock::now();  // 记录测试结束时间
    // 计算总耗时（秒）
    double durationSec = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count()) / 1000000.0;

    // 填充结果结构体
    BenchmarkResult result;
    result.totalOps = totalOps.load();
    result.totalHits = totalHits.load();
    result.totalMisses = totalMisses.load();
    result.durationSec = durationSec;
    result.opsPerSec = durationSec > 0 ? (static_cast<double>(result.totalOps) / durationSec) : 0;
    result.latenciesUs = std::move(latencies);

    return result;
}

// 打印基准测试报告：吞吐量、命中率、延迟分位数（p50/p90/p99/p99.9/max）
static void printResults(const BenchmarkResult& r) {
    std::vector<double> lats = r.latenciesUs;
    std::sort(lats.begin(), lats.end());  // 升序排序以便计算分位数

    // 迟分位数计算 lambda
    auto percentile = [&](double p) -> double {
        if (lats.empty()) return 0;
        size_t idx = static_cast<size_t>(p / 100.0 * (lats.size() - 1));
        if (idx >= lats.size()) idx = lats.size() - 1;
        return lats[idx];
    };

    // 平均延迟
    double avgUs = lats.empty() ? 0 :
        std::accumulate(lats.begin(), lats.end(), 0.0) / static_cast<double>(lats.size());

    // 缓存命中率
    uint64_t totalGets = r.totalHits + r.totalMisses;
    double hitRate = totalGets > 0 ? (100.0 * r.totalHits / totalGets) : 0.0;

    // 格式化输出报告
    std::cout << "\n"
              << "========== Benchmark Results ==========\n"
              << "Total requests:      " << r.totalOps << "\n"
              << "Duration:            " << std::fixed << std::setprecision(3) << r.durationSec << " sec\n"
              << "Throughput:          " << std::fixed << std::setprecision(0) << r.opsPerSec << " ops/sec\n"
              << "Cache hit rate:      " << std::fixed << std::setprecision(2) << hitRate << "%\n"
              << "\n"
              << "Latency (microseconds):\n"
              << "  Average:           " << std::fixed << std::setprecision(1) << avgUs << " us\n"
              << "  p50:               " << std::fixed << std::setprecision(1) << percentile(50) << " us\n"
              << "  p90:               " << std::fixed << std::setprecision(1) << percentile(90) << " us\n"
              << "  p99:               " << std::fixed << std::setprecision(1) << percentile(99) << " us\n"
              << "  p99.9:             " << std::fixed << std::setprecision(1) << percentile(99.9) << " us\n"
              << "  Max:               " << std::fixed << std::setprecision(1) << percentile(100) << " us\n"
              << "=======================================\n"
              << std::flush;
}

// 基准测试程序入口：解析参数 → 运行测试 → 打印报告
int main(int argc, char* argv[]) {
    BenchmarkConfig cfg;  // 使用默认配置初始化

    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        // 帮助选项
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --host HOST          Server host (default: 127.0.0.1)\n"
                      << "  --port PORT          Server port (default: 9999)\n"
                      << "  --connections N      Concurrent connections (default: 10)\n"
                      << "  --requests N         Requests per connection (default: 10000)\n"
                      << "  --keyspace N         Number of unique keys (default: 1000)\n"
                      << "  --read-ratio R       Fraction of GET vs SET, 0.0-1.0 (default: 0.8)\n"
                      << "  --value-size N       Value size in bytes (default: 64)\n"
                      << "  --help               Show this help\n"
                      << std::flush;
            return 0;
        }

        // 辅助 lambda：获取选项的下一个参数值
        auto nextVal = [&]() -> const char* {
            if (i + 1 < argc) return argv[++i];
            std::cerr << "Error: " << arg << " requires a value\n";
            std::exit(1);
        };

        // 逐个匹配选项
        if (arg == "--host")            cfg.host = nextVal();
        else if (arg == "--port")       cfg.port = std::atoi(nextVal());
        else if (arg == "--connections") cfg.connections = std::atoi(nextVal());
        else if (arg == "--requests")   cfg.requestsPerConn = std::atoi(nextVal());
        else if (arg == "--keyspace")   cfg.keySpace = std::atoi(nextVal());
        else if (arg == "--read-ratio") cfg.readRatio = std::atof(nextVal());
        else if (arg == "--value-size") cfg.valueSize = std::atoi(nextVal());
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
    }

    // 参数合法性校验
    if (cfg.connections <= 0 || cfg.requestsPerConn <= 0 || cfg.keySpace <= 0) {
        std::cerr << "Error: connections, requests, and keyspace must be positive\n";
        return 1;
    }

    // 运行基准测试并输出结果
    BenchmarkResult result = runBenchmark(cfg);
    printResults(result);

    return 0;
}
