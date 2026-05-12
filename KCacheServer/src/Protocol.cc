// 包含自己的头文件声明
#include "Protocol.h"
// 引入 muduo Buffer 类，用于读取原始字节数据
#include "Buffer.h"

// 引入 C 字符串操作函数（如 strlen）
#include <cstring>
// 引入 std::ostringstream，用于高效拼接多行字符串（STATS 响应）
#include <sstream>

// KCacheServer 命名空间实现
namespace KCacheServer {

// 将字符串转换为大写：用于命令名大小写不敏感匹配（如 "get" -> "GET"）
std::string Protocol::toUpper(const std::string& s) {
    std::string result = s;         // 复制输入字符串
    for (auto& c : result) {        // 遍历字符串中的每个字符
        // 如果字符是小写字母（ASCII 'a'~'z'），则转换为大写
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    }
    return result;
}

// 解析单行文本为 Request 结构体
void Protocol::parseLine(const std::string& line, Request* req) {
    // 初始化请求为空状态
    *req = Request{};

    // 空行直接返回，命令保持 UNKNOWN
    if (line.empty()) {
        return;
    }

    // 查找第一个空格，用于分割命令和参数
    size_t pos1 = line.find(' ');

    // 提取命令字符串：如果有空格则取空格前的子串，否则取整行
    std::string cmdStr = (pos1 == std::string::npos) ? line : line.substr(0, pos1);

    // 将命令转换为大写，实现大小写不敏感匹配
    cmdStr = toUpper(cmdStr);

    // PING 命令：无参数心跳检测
    if (cmdStr == "PING") {
        req->cmd = Command::PING;
        return;
    }

    // STATS 命令：无参数查询统计信息
    if (cmdStr == "STATS") {
        req->cmd = Command::STATS;
        return;
    }

    // QUIT 命令：无参数断开连接
    if (cmdStr == "QUIT") {
        req->cmd = Command::QUIT;
        return;
    }

    // 以下命令（GET/SET/DEL）需要 key 参数
    if (pos1 == std::string::npos) {
        // 没有提供参数（无空格），但命令名本身有效则保留命令类型
        if (cmdStr == "GET")      { req->cmd = Command::GET; return; }
        if (cmdStr == "DEL")      { req->cmd = Command::DEL; return; }
        if (cmdStr == "SET")      { req->cmd = Command::SET; return; }
        // 完全不认识的无参数命令，标记为 UNKNOWN
        req->cmd = Command::UNKNOWN;
        return;
    }

    // 提取命令参数部分（第一个空格之后的所有内容）
    std::string rest = line.substr(pos1 + 1);

    // GET 命令：key 就是剩余的全部内容
    if (cmdStr == "GET") {
        req->cmd = Command::GET;
        req->key = rest;
        return;
    }

    // DEL 命令：key 就是剩余的全部内容
    if (cmdStr == "DEL") {
        req->cmd = Command::DEL;
        req->key = rest;
        return;
    }

    // SET 命令：需要从剩余内容中分割 key 和 value
    if (cmdStr == "SET") {
        // 查找 key 和 value 之间的分隔空格
        size_t pos2 = rest.find(' ');
        if (pos2 == std::string::npos) {
            // SET 只有 key 没有 value 的情况：存储空字符串作为 value
            req->cmd = Command::SET;
            req->key = rest;
            return;
        }
        // 正常情况：key 为第二个空格前的内容，value 为第二个空格后的全部
        req->cmd = Command::SET;
        req->key = rest.substr(0, pos2);
        req->value = rest.substr(pos2 + 1);
        return;
    }

    // 无法识别的命令
    req->cmd = Command::UNKNOWN;
}

// 从缓冲区中提取一条完整命令（以 \r\n 或 \n 为行终止符）
bool Protocol::parse(Buffer* buf, Request* req) {
    const char* data = buf->peek();       // 获取缓冲区数据的只读指针（不消费）
    size_t len = buf->readableBytes();   // 获取可读数据的字节数

    // 优先搜索 \r\n 终止符（标准行尾格式）
    for (size_t i = 0; i + 1 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            // 提取行内容（不包括 \r\n）
            std::string line(data, i);
            // 从缓冲区中消费已解析的数据（行内容 + \r\n 共 i+2 字节）
            buf->retrieve(i + 2);
            // 解析行文本为请求结构体
            parseLine(line, req);
            return true;  // 成功解析一条命令
        }
    }

    // 兼容处理：也接受单独的 \n 作为行终止符（增强鲁棒性）
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == '\n') {
            // 提取行内容（不包括 \n）
            std::string line(data, i);
            // 从缓冲区中消费已解析的数据（行内容 + \n 共 i+1 字节）
            buf->retrieve(i + 1);
            // 解析行文本为请求结构体
            parseLine(line, req);
            return true;  // 成功解析一条命令
        }
    }

    // 没有找到完整的行终止符，需要等待更多数据到达
    return false;
}

// 编码 GET 成功响应：返回 "VALUE <value>\r\n" 格式字符串
std::string Protocol::encodeGetResponse(const std::string& value) {
    return "VALUE " + value + "\r\n";
}

// 编码"键不存在"响应：返回 "NIL\r\n"
std::string Protocol::encodeNil() {
    return "NIL\r\n";
}

// 编码操作成功响应：返回 "OK\r\n"
std::string Protocol::encodeOk() {
    return "OK\r\n";
}

// 编码 PING 响应：返回 "PONG\r\n"
std::string Protocol::encodePong() {
    return "PONG\r\n";
}

// 编码错误响应：返回 "ERR <message>\r\n" 格式
std::string Protocol::encodeError(const std::string& msg) {
    return "ERR " + msg + "\r\n";
}

// 编码统计信息响应：返回多行 key:value 格式的统计信息
std::string Protocol::encodeStats(
    uint64_t hits, uint64_t misses, uint64_t sets,
    uint64_t dels, uint64_t connections, uint64_t pings,
    uint64_t uptimeSec)
{
    // 计算总 GET 请求数和缓存命中率
    uint64_t totalGets = hits + misses;
    double hitRate = (totalGets > 0) ? (100.0 * hits / totalGets) : 0.0;

    // 使用 ostringstream 高效拼接多行字符串
    std::ostringstream oss;
    oss << "hits:" << hits << "\r\n"               // 写入命中数
        << "misses:" << misses << "\r\n"           // 写入未命中数
        << "total_gets:" << totalGets << "\r\n"    // 写入 GET 请求总数
        << "hit_rate:" << hitRate << "%\r\n"       // 写入命中率（百分比）
        << "sets:" << sets << "\r\n"               // 写入写入操作数
        << "dels:" << dels << "\r\n"               // 写入删除操作数
        << "connections:" << connections << "\r\n" // 写入连接数
        << "pings:" << pings << "\r\n"             // 写入心跳数
        << "uptime_sec:" << uptimeSec << "\r\n";   // 写入运行秒数

    // 返回拼接后的完整统计字符串
    return oss.str();
}

} // namespace KCacheServer
