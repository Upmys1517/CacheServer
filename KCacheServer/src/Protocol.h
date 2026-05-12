#pragma once

#include <cstdint>
#include <string>

class Buffer;

namespace KCacheServer {

enum class Command {
    GET,
    SET,
    DEL,
    PING,
    STATS,
    QUIT,
    UNKNOWN
};

struct Request {
    Command cmd = Command::UNKNOWN;
    std::string key;
    std::string value;
};

class Protocol {
public:
    // Extract one complete command from buffer. Returns true if a complete
    // command was found (buffer is advanced past the consumed line).
    // Returns false if more data is needed.
    static bool parse(Buffer* buf, Request* req);

    // Response encoders
    static std::string encodeGetResponse(const std::string& value);
    static std::string encodeNil();
    static std::string encodeOk();
    static std::string encodePong();
    static std::string encodeError(const std::string& msg);
    static std::string encodeStats(
        uint64_t hits, uint64_t misses, uint64_t sets,
        uint64_t dels, uint64_t connections, uint64_t pings,
        uint64_t uptimeSec);

private:
    static void parseLine(const std::string& line, Request* req);
    static std::string toUpper(const std::string& s);
};

} // namespace KCacheServer
