#include "Protocol.h"
#include "Buffer.h"

#include <cstring>
#include <sstream>

namespace KCacheServer {

std::string Protocol::toUpper(const std::string& s) {
    std::string result = s;
    for (auto& c : result) {
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    }
    return result;
}

void Protocol::parseLine(const std::string& line, Request* req) {
    *req = Request{};
    if (line.empty()) {
        return;
    }

    // Find first token (command)
    size_t pos1 = line.find(' ');
    std::string cmdStr = (pos1 == std::string::npos) ? line : line.substr(0, pos1);
    cmdStr = toUpper(cmdStr);

    if (cmdStr == "PING") {
        req->cmd = Command::PING;
        return;
    }
    if (cmdStr == "STATS") {
        req->cmd = Command::STATS;
        return;
    }
    if (cmdStr == "QUIT") {
        req->cmd = Command::QUIT;
        return;
    }

    // Commands that require a key
    if (pos1 == std::string::npos) {
        // No argument provided
        if (cmdStr == "GET")      { req->cmd = Command::GET; return; }
        if (cmdStr == "DEL")      { req->cmd = Command::DEL; return; }
        if (cmdStr == "SET")      { req->cmd = Command::SET; return; }
        req->cmd = Command::UNKNOWN;
        return;
    }

    std::string rest = line.substr(pos1 + 1);

    if (cmdStr == "GET") {
        req->cmd = Command::GET;
        req->key = rest;
        return;
    }

    if (cmdStr == "DEL") {
        req->cmd = Command::DEL;
        req->key = rest;
        return;
    }

    if (cmdStr == "SET") {
        // Split key from value: key is first token, value is the rest
        size_t pos2 = rest.find(' ');
        if (pos2 == std::string::npos) {
            // SET with only key, no value — store empty string
            req->cmd = Command::SET;
            req->key = rest;
            return;
        }
        req->cmd = Command::SET;
        req->key = rest.substr(0, pos2);
        req->value = rest.substr(pos2 + 1);
        return;
    }

    req->cmd = Command::UNKNOWN;
}

bool Protocol::parse(Buffer* buf, Request* req) {
    const char* data = buf->peek();
    size_t len = buf->readableBytes();

    // Search for \r\n terminator
    for (size_t i = 0; i + 1 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            std::string line(data, i);
            buf->retrieve(i + 2);  // consume line + \r\n
            parseLine(line, req);
            return true;
        }
    }

    // Also handle bare \n for robustness
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == '\n') {
            std::string line(data, i);
            buf->retrieve(i + 1);  // consume line + \n
            parseLine(line, req);
            return true;
        }
    }

    return false;
}

std::string Protocol::encodeGetResponse(const std::string& value) {
    return "VALUE " + value + "\r\n";
}

std::string Protocol::encodeNil() {
    return "NIL\r\n";
}

std::string Protocol::encodeOk() {
    return "OK\r\n";
}

std::string Protocol::encodePong() {
    return "PONG\r\n";
}

std::string Protocol::encodeError(const std::string& msg) {
    return "ERR " + msg + "\r\n";
}

std::string Protocol::encodeStats(
    uint64_t hits, uint64_t misses, uint64_t sets,
    uint64_t dels, uint64_t connections, uint64_t pings,
    uint64_t uptimeSec)
{
    uint64_t totalGets = hits + misses;
    double hitRate = (totalGets > 0) ? (100.0 * hits / totalGets) : 0.0;

    std::ostringstream oss;
    oss << "hits:" << hits << "\r\n"
        << "misses:" << misses << "\r\n"
        << "total_gets:" << totalGets << "\r\n"
        << "hit_rate:" << hitRate << "%\r\n"
        << "sets:" << sets << "\r\n"
        << "dels:" << dels << "\r\n"
        << "connections:" << connections << "\r\n"
        << "pings:" << pings << "\r\n"
        << "uptime_sec:" << uptimeSec << "\r\n";
    return oss.str();
}

} // namespace KCacheServer
