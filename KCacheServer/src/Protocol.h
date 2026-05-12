// 预处理指令：确保头文件在整个编译过程中只被包含一次
#pragma once

// 引入固定宽度整数类型：uint64_t
#include <cstdint>
// 引入 std::string 字符串类
#include <string>

// 前向声明 muduo 的 Buffer 类，避免包含完整头文件
class Buffer;

// KCacheServer 命名空间：协议解析和响应编码
namespace KCacheServer {

// 命令枚举类：定义服务器支持的所有命令类型
enum class Command {
    GET,      // 根据 key 获取缓存值
    SET,      // 设置 key 对应的缓存值
    DEL,      // 删除指定 key 的缓存条目
    PING,     // 心跳检测命令，用于保活和延迟测量
    STATS,    // 查询服务器运行统计信息
    QUIT,     // 客户端主动断开连接
    UNKNOWN   // 未识别的命令，返回错误
};

// 请求结构体：存储一条解析后的命令及其参数
struct Request {
    Command cmd = Command::UNKNOWN; // 解析出的命令类型，默认 UNKNOWN
    std::string key;                // 命令的 key 参数（GET/SET/DEL 使用）
    std::string value;              // SET 命令的值参数（仅在 SET 时有效）
};

// Protocol 协议类：纯静态方法，负责二进制协议解析和响应消息编码
// 自定义文本协议，每条命令以 \r\n（或 \n）结束
class Protocol {
public:
    // 从缓冲区中提取一条完整的命令。如果找到完整命令则返回 true，
    // 同时将缓冲区指针前移（消耗已解析的数据）。
    // 如果没有找到完整的行终止符 \r\n，则返回 false，等待更多数据。
    static bool parse(Buffer* buf, Request* req);

    // ====== 响应消息编码器：将内部结果转换为符合协议的字符串 ======

    // 编码 GET 成功的响应："VALUE <value>\r\n"
    static std::string encodeGetResponse(const std::string& value);

    // 编码 GET 未命中的响应："NIL\r\n"
    static std::string encodeNil();

    // 编码操作成功的响应："OK\r\n"
    static std::string encodeOk();

    // 编码 PING 的响应："PONG\r\n"
    static std::string encodePong();

    // 编码错误响应："ERR <message>\r\n"
    static std::string encodeError(const std::string& msg);

    // 编码统计信息响应：多行 key:value 格式，每行以 \r\n 结束
    static std::string encodeStats(
        uint64_t hits, uint64_t misses, uint64_t sets,
        uint64_t dels, uint64_t connections, uint64_t pings,
        uint64_t uptimeSec);

private:
    // 解析单行文本为 Request 结构体，识别命令和参数
    static void parseLine(const std::string& line, Request* req);

    // 将字符串转换为大写，用于命令名大小写不敏感匹配
    static std::string toUpper(const std::string& s);
};

} // namespace KCacheServer
