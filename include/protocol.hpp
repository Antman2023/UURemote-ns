#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace uu {
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
using Headers = std::map<std::string, std::string>;
std::string sign(const std::string &key, const std::string &method, const std::string &path,
                 const Headers &headers, const std::string &body);
Headers requestHeaders(const std::string &key, const std::string &method, const std::string &path,
                       const std::string &body, const std::string &client, const std::string &device,
                       const std::string &user, int64_t timestamp);
Bytes connectOptions(int width = 1280, int height = 720, int fps = 30, int quality = 3);
Bytes launchApp(uint64_t seq, const Json &app);
Json ackValue(const Json &args, const std::string &event);
Json soac(const Json &client, const std::string &type, const Json &payload, const Json &ice,
          const std::string &control);
std::string signalingUrl(const std::string &source);
std::string uuid();
Bytes randomBytes(size_t size);
int64_t nowMs();
std::string base64(const uint8_t *data, size_t size);
std::string percentEncode(const std::string &value);

// Incremental RFC 6455 parser. All advertised lengths are bounded before allocation.
struct WsMessage {
    uint8_t opcode;
    Bytes data;
};
class WsParser {
    Bytes pending_, fragment_;
    uint8_t fragmentOpcode_ = 0;

  public:
    std::vector<WsMessage> feed(const uint8_t *data, size_t size);
};
Bytes wsFrame(uint8_t opcode, const Bytes &payload, std::array<uint8_t, 4> mask);
struct SioPacket {
    int type = -1, id = -1, attachments = 0;
    Json data;
};
SioPacket parseSocketIo(const std::string &packet);
} // namespace uu
