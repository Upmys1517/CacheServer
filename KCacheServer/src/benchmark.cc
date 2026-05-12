#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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

using Clock = std::chrono::high_resolution_clock;

struct BenchmarkConfig {
    std::string host = "127.0.0.1";
    int port = 9999;
    int connections = 10;
    int requestsPerConn = 10000;
    int keySpace = 1000;
    double readRatio = 0.8;
    int valueSize = 64;
};

struct BenchmarkResult {
    uint64_t totalOps = 0;
    uint64_t totalHits = 0;
    uint64_t totalMisses = 0;
    double durationSec = 0;
    double opsPerSec = 0;
    std::vector<double> latenciesUs;  // microseconds
};

// Generate a random alphanumeric string of given length
static std::string randomValue(int len, std::mt19937& rng) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::uniform_int_distribution<int> dist(0, sizeof(chars) - 2);
    std::string s(len, '\0');
    for (int i = 0; i < len; ++i) s[i] = chars[dist(rng)];
    return s;
}

static std::string makeKey(int i) {
    std::ostringstream oss;
    oss << "key:" << std::setw(7) << std::setfill('0') << i;
    return oss.str();
}

// Read a response line (up to \r\n) from socket, with internal buffering.
// Returns the line without \r\n.
static std::string readResponse(int fd, std::string& recvBuf) {
    char tmp[4096];
    while (true) {
        // Search for \r\n in existing buffer
        size_t pos = recvBuf.find("\r\n");
        if (pos != std::string::npos) {
            std::string line = recvBuf.substr(0, pos);
            recvBuf.erase(0, pos + 2);
            return line;
        }
        // Read more data
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) return "";  // connection closed or error
        recvBuf.append(tmp, static_cast<size_t>(n));
    }
}

static void benchmarkWorker(int connId,
                            const BenchmarkConfig& cfg,
                            std::atomic<uint64_t>& totalOps,
                            std::atomic<uint64_t>& totalHits,
                            std::atomic<uint64_t>& totalMisses,
                            std::vector<double>& latencies,
                            std::mutex& latMutex)
{
    // Create socket
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "Worker " << connId << ": socket() failed\n";
        return;
    }

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(cfg.port));
    if (::inet_pton(AF_INET, cfg.host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "Worker " << connId << ": invalid address\n";
        ::close(fd);
        return;
    }

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Worker " << connId << ": connect() failed\n";
        ::close(fd);
        return;
    }

    std::mt19937 rng(std::random_device{}() + connId);
    std::uniform_int_distribution<int> keyDist(0, cfg.keySpace - 1);
    std::uniform_real_distribution<double> opDist(0.0, 1.0);

    std::string recvBuf;
    std::vector<double> localLatencies;
    localLatencies.reserve(static_cast<size_t>(cfg.requestsPerConn));

    uint64_t localHits = 0;
    uint64_t localMisses = 0;

    for (int i = 0; i < cfg.requestsPerConn; ++i) {
        std::string key = makeKey(keyDist(rng));
        std::string request;

        if (opDist(rng) < cfg.readRatio) {
            // GET
            request = "GET " + key + "\r\n";
        } else {
            // SET
            std::string value = randomValue(cfg.valueSize, rng);
            request = "SET " + key + " " + value + "\r\n";
        }

        auto t1 = Clock::now();

        // Send request
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

        // Read response
        std::string line = readResponse(fd, recvBuf);
        if (line.empty()) {
            std::cerr << "Worker " << connId << ": connection closed by server\n";
            ::close(fd);
            return;
        }

        auto t2 = Clock::now();
        double us = static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count());
        localLatencies.push_back(us);

        // Track hits/misses for GET
        if (request[0] == 'G') {
            if (line.rfind("VALUE", 0) == 0) {
                localHits++;
            } else {
                localMisses++;
            }
        }
    }

    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);

    // Merge results
    totalOps += static_cast<uint64_t>(cfg.requestsPerConn);
    totalHits += localHits;
    totalMisses += localMisses;

    {
        std::lock_guard<std::mutex> lock(latMutex);
        latencies.insert(latencies.end(), localLatencies.begin(), localLatencies.end());
    }
}

static BenchmarkResult runBenchmark(const BenchmarkConfig& cfg) {
    std::atomic<uint64_t> totalOps{0};
    std::atomic<uint64_t> totalHits{0};
    std::atomic<uint64_t> totalMisses{0};
    std::vector<double> latencies;
    std::mutex latMutex;

    std::cout << "Starting benchmark:\n"
              << "  target:    " << cfg.host << ":" << cfg.port << "\n"
              << "  connections: " << cfg.connections << "\n"
              << "  requests/conn: " << cfg.requestsPerConn << "\n"
              << "  total requests: " << (cfg.connections * cfg.requestsPerConn) << "\n"
              << "  key space: " << cfg.keySpace << "\n"
              << "  read ratio: " << (cfg.readRatio * 100) << "%\n"
              << "  value size: " << cfg.valueSize << " bytes\n"
              << "Running..." << std::endl;

    auto t1 = Clock::now();

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(cfg.connections));
    for (int i = 0; i < cfg.connections; ++i) {
        workers.emplace_back(benchmarkWorker, i, std::ref(cfg),
                             std::ref(totalOps), std::ref(totalHits),
                             std::ref(totalMisses),
                             std::ref(latencies), std::ref(latMutex));
    }

    for (auto& t : workers) {
        t.join();
    }

    auto t2 = Clock::now();
    double durationSec = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count()) / 1000000.0;

    BenchmarkResult result;
    result.totalOps = totalOps.load();
    result.totalHits = totalHits.load();
    result.totalMisses = totalMisses.load();
    result.durationSec = durationSec;
    result.opsPerSec = durationSec > 0 ? (static_cast<double>(result.totalOps) / durationSec) : 0;
    result.latenciesUs = std::move(latencies);

    return result;
}

static void printResults(const BenchmarkResult& r) {
    std::vector<double> lats = r.latenciesUs;
    std::sort(lats.begin(), lats.end());

    auto percentile = [&](double p) -> double {
        if (lats.empty()) return 0;
        size_t idx = static_cast<size_t>(p / 100.0 * (lats.size() - 1));
        if (idx >= lats.size()) idx = lats.size() - 1;
        return lats[idx];
    };

    double avgUs = lats.empty() ? 0 :
        std::accumulate(lats.begin(), lats.end(), 0.0) / static_cast<double>(lats.size());

    uint64_t totalGets = r.totalHits + r.totalMisses;
    double hitRate = totalGets > 0 ? (100.0 * r.totalHits / totalGets) : 0.0;

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

int main(int argc, char* argv[]) {
    BenchmarkConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

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

        auto nextVal = [&]() -> const char* {
            if (i + 1 < argc) return argv[++i];
            std::cerr << "Error: " << arg << " requires a value\n";
            std::exit(1);
        };

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

    if (cfg.connections <= 0 || cfg.requestsPerConn <= 0 || cfg.keySpace <= 0) {
        std::cerr << "Error: connections, requests, and keyspace must be positive\n";
        return 1;
    }

    BenchmarkResult result = runBenchmark(cfg);
    printResults(result);

    return 0;
}
