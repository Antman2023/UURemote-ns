#include "protocol.hpp"
#include <cctype>
#include <chrono>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <random>
#include <stdexcept>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace uu {
int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
static std::string hex(const uint8_t *bytes, size_t size) {
    static constexpr char chars[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out += chars[bytes[i] >> 4];
        out += chars[bytes[i] & 15];
    }
    return out;
}
Bytes randomBytes(size_t size) {
    Bytes b(size);
#ifdef __SWITCH__
    randomGet(b.data(), b.size());
#else
    std::random_device random;
    for (auto &v : b)
        v = static_cast<uint8_t>(random());
#endif
    return b;
}
std::string uuid() {
    auto b = randomBytes(16);
    b[6] = (b[6] & 15) | 0x40;
    b[8] = (b[8] & 63) | 0x80;
    auto h = hex(b.data(), b.size());
    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" +
           h.substr(20);
}
std::string base64(const uint8_t *data, size_t size) {
    size_t len = 0;
    std::string out(4 * ((size + 2) / 3) + 1, '\0');
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char *>(out.data()), out.size(), &len, data, size))
        throw std::runtime_error("Base64 encoding failed");
    out.resize(len);
    return out;
}
std::string percentEncode(const std::string &s) {
    std::string out;
    const char *h = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += c;
        else {
            out += '%';
            out += h[c >> 4];
            out += h[c & 15];
        }
    }
    return out;
}
std::string sign(const std::string &key, const std::string &method, const std::string &path,
                 const Headers &headers, const std::string &body) {
    Headers canonical;
    for (const auto &[name, value] : headers) {
        auto n = name;
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
        if (n.rfind("x-param-", 0) == 0 && n != "x-param-sign")
            canonical[n] = value;
    }
    std::string text = method + path;
    bool first = true;
    for (const auto &[name, value] : canonical) {
        if (!first)
            text += '&';
        first = false;
        text += name + '=' + value;
    }
    text += body;
    uint8_t digest[32];
    if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                        reinterpret_cast<const uint8_t *>(key.data()), key.size(),
                        reinterpret_cast<const uint8_t *>(text.data()), text.size(), digest))
        throw std::runtime_error("Request signing failed");
    return hex(digest, 32);
}
Headers requestHeaders(const std::string &key, const std::string &method, const std::string &path,
                       const std::string &body, const std::string &client, const std::string &device,
                       const std::string &user, int64_t timestamp) {
    Headers h = {{"X-Param-CHN", "gwqd"},
                 {"X-Param-OPR", ""},
                 {"X-Param-PKGN", "com.netease.uuremote"},
                 {"X-Param-PLAT", "5"},
                 {"X-Param-REL", "prod"},
                 {"X-Param-TS", std::to_string(timestamp)},
                 {"X-Param-VC", "30500"},
                 {"X-Param-VN", "3.5.0"},
                 {"X-Param-ABI", "arm64-v8a"},
                 {"X-Param-client-id", client},
                 {"X-Param-device-id", device},
                 {"X-Param-user-id", user},
                 {"X-Param-OAID", ""},
                 {"X-Param-CNT", "CN"},
                 {"X-Param-LANG", "zh-CN"}};
    h["X-Param-SIGN"] = sign(key, method, path, h, body);
    h["Content-Type"] = "application/json; charset=utf-8";
    return h;
}
static void varint(Bytes &b, uint64_t n) {
    while (n > 127) {
        b.push_back((n & 127) | 128);
        n >>= 7;
    }
    b.push_back(n);
}
static void integer(Bytes &b, int field, uint64_t n) {
    varint(b, field * 8);
    varint(b, n);
}
static void message(Bytes &b, int field, const Bytes &s) {
    varint(b, field * 8 + 2);
    varint(b, s.size());
    b.insert(b.end(), s.begin(), s.end());
}
static void text(Bytes &b, int field, const std::string &s) {
    message(b, field, Bytes(s.begin(), s.end()));
}
Bytes connectOptions(int w, int h, int fps, int quality) {
    Bytes resolution, capture, decoder, out;
    integer(resolution, 1, w);
    integer(resolution, 2, h);
    integer(capture, 1, fps == 30 ? 1 : 2);
    integer(capture, 2, quality);
    integer(capture, 3, 1);
    integer(capture, 4, 2);
    message(capture, 5, resolution);
    message(capture, 6, resolution);
    integer(decoder, 1, fps);
    integer(decoder, 2, 1);
    integer(decoder, 3, w);
    integer(decoder, 4, h);
    integer(out, 1, 1);
    integer(out, 2, uint64_t(-1));
    message(out, 3, capture);
    message(out, 4, decoder);
    integer(out, 8, 2);
    return out;
}
Bytes launchApp(uint64_t seq, const Json &app) {
    Bytes p, out;
    text(p, 1, app.value("dapp_id", std::string{}));
    text(p, 2, app.value("name", std::string{}));
    text(p, 3, app.value("target_path", std::string{}));
    integer(out, 1, seq);
    integer(out, 2, nowMs());
    message(out, 4, p);
    return out;
}
Json ackValue(const Json &args, const std::string &event) {
    if (!args.is_array() || args.empty())
        throw std::runtime_error(event + ": empty acknowledgment");
    Json value;
    if (args.size() == 1 && args[0].is_object())
        value = args[0];
    else {
        auto e = args[0];
        if (!e.is_null() && e != false && e != 0 && e != "" && e != "succ" && e != "success" &&
            !(e.is_object() && e.value("code", -1) == 0))
            throw std::runtime_error(event + ": request rejected");
        if (args.size() < 2)
            throw std::runtime_error(event + ": missing response");
        value = args[1];
    }
    if (value.is_object() && value.contains("code") && value["code"] != 0)
        throw std::runtime_error(event + ": " + value.value("msg", std::string("request rejected")));
    return value;
}
Json soac(const Json &client, const std::string &type, const Json &payload, const Json &ice,
          const std::string &control) {
    Json data = type == "candidate" ? Json{{"candidate", payload}} : payload;
    data["type"] = type;
    data["ice_id"] = ice;
    data["app_control_id"] = control;
    return {{"client_id", client}, {"data", data}};
}
std::string signalingUrl(const std::string &source) {
    std::string s = source.find("://") == std::string::npos ? "wss://" + source : source;
    if (s.rfind("wss://", 0) != 0)
        throw std::runtime_error("Signaling requires WSS");
    auto end = s.find_first_of("/?#", 6);
    auto authority = s.substr(6, end == std::string::npos ? end : end - 6);
    auto colon = authority.find(':');
    auto host = authority.substr(0, colon);
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return std::tolower(c); });
    if (host != "163.com" && (host.size() < 8 || host.substr(host.size() - 8) != ".163.com"))
        throw std::runtime_error("Untrusted signaling host");
    if (authority.find('@') != std::string::npos)
        throw std::runtime_error("Invalid signaling address");
    return "wss://" + authority + "/socket.io/?EIO=4&transport=websocket";
}
Bytes wsFrame(uint8_t opcode, const Bytes &payload, std::array<uint8_t, 4> mask) {
    Bytes b{uint8_t(0x80 | opcode)};
    size_t n = payload.size();
    if (n < 126)
        b.push_back(0x80 | n);
    else if (n <= 65535) {
        b.push_back(0xfe);
        b.push_back(n >> 8);
        b.push_back(n);
    } else {
        b.push_back(0xff);
        for (int i = 7; i >= 0; --i)
            b.push_back(uint64_t(n) >> (i * 8));
    }
    b.insert(b.end(), mask.begin(), mask.end());
    for (size_t i = 0; i < n; ++i)
        b.push_back(payload[i] ^ mask[i % 4]);
    return b;
}
std::vector<WsMessage> WsParser::feed(const uint8_t *data, size_t size) {
    constexpr size_t max = 2 * 1024 * 1024;
    if (pending_.size() + size > max + 14)
        throw std::runtime_error("WebSocket buffer limit");
    pending_.insert(pending_.end(), data, data + size);
    std::vector<WsMessage> out;
    size_t p = 0;
    while (pending_.size() - p >= 2) {
        auto *b = pending_.data() + p;
        bool fin = b[0] & 128;
        uint8_t op = b[0] & 15;
        size_t head = 2;
        uint64_t n = b[1] & 127;
        if ((b[0] & 0x70) || (b[1] & 128))
            throw std::runtime_error("Invalid server WebSocket frame");
        if (n == 126) {
            if (pending_.size() - p < 4)
                break;
            n = (b[2] << 8) | b[3];
            head = 4;
        } else if (n == 127) {
            if (pending_.size() - p < 10)
                break;
            n = 0;
            for (int i = 2; i < 10; ++i)
                n = (n << 8) | b[i];
            head = 10;
        }
        if (n > max || (op >= 8 && (!fin || n > 125)))
            throw std::runtime_error("Invalid WebSocket length");
        if (n > pending_.size() - p - head)
            break;
        Bytes payload(b + head, b + head + n);
        p += head + n;
        if (op >= 8) {
            if (op != 8 && op != 9 && op != 10)
                throw std::runtime_error("Invalid control opcode");
            out.push_back({op, std::move(payload)});
        } else if (op == 0) {
            if (!fragmentOpcode_)
                throw std::runtime_error("Unexpected continuation");
            if (fragment_.size() + n > max)
                throw std::runtime_error("Fragment limit");
            fragment_.insert(fragment_.end(), payload.begin(), payload.end());
            if (fin) {
                out.push_back({fragmentOpcode_, std::move(fragment_)});
                fragment_.clear();
                fragmentOpcode_ = 0;
            }
        } else if (op == 1 || op == 2) {
            if (fragmentOpcode_)
                throw std::runtime_error("Interleaved WebSocket message");
            if (fin)
                out.push_back({op, std::move(payload)});
            else {
                fragmentOpcode_ = op;
                fragment_ = std::move(payload);
            }
        } else
            throw std::runtime_error("Invalid WebSocket opcode");
    }
    pending_.erase(pending_.begin(), pending_.begin() + p);
    return out;
}
SioPacket parseSocketIo(const std::string &s) {
    SioPacket p;
    if (s.size() < 2 || s[0] != '4' || s[1] < '0' || s[1] > '6')
        throw std::runtime_error("Invalid Socket.IO packet");
    p.type = s[1] - '0';
    size_t i = 2;
    auto number = [&]() {
        int n = 0;
        size_t start = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            if (n > 1000000)
                throw std::runtime_error("Socket.IO number limit");
            n = n * 10 + s[i++] - '0';
        }
        if (i == start)
            throw std::runtime_error("Missing Socket.IO number");
        return n;
    };
    if (p.type == 5 || p.type == 6) {
        p.attachments = number();
        if (p.attachments > 16 || i >= s.size() || s[i++] != '-')
            throw std::runtime_error("Invalid attachments");
    }
    if (i < s.size() && s[i] == '/')
        throw std::runtime_error("Unsupported Socket.IO namespace");
    if (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
        p.id = number();
    if (i < s.size())
        p.data = Json::parse(s.substr(i));
    return p;
}
} // namespace uu
