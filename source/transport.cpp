#include "transport.hpp"
#include <chrono>
#include <mbedtls/sha1.h>
#include <stdexcept>
#include <thread>

namespace uu {
namespace {
struct Curl {
    CURL *handle = curl_easy_init();
    curl_slist *headers = nullptr;
    ~Curl() {
        curl_slist_free_all(headers);
        if (handle)
            curl_easy_cleanup(handle);
    }
    void header(const std::string &k, const std::string &v) {
        headers = curl_slist_append(headers, (v.empty() ? k + ";" : k + ": " + v).c_str());
    }
};
size_t append(char *p, size_t size, size_t n, void *user) {
    auto &out = *static_cast<std::string *>(user);
    if (out.size() + size * n > 2 * 1024 * 1024)
        return 0;
    out.append(p, size * n);
    return size * n;
}
int progress(void *cancel, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<std::atomic<bool> *>(cancel)->load() ? 1 : 0;
}
void common(CURL *c, std::atomic<bool> &cancel, int timeout) {
    if (!c)
        throw std::runtime_error("Cannot allocate HTTP client");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 15000L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, long(timeout));
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &cancel);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
}
} // namespace
Json Api::request(const std::string &method, const std::string &path, const Json &data, int timeout) {
    if (cancel_)
        throw std::runtime_error("Canceled");
    Curl c;
    common(c.handle, cancel_, timeout);
    const auto body =
        data.is_null() ? std::string{} : data.dump(-1, ' ', false, Json::error_handler_t::strict);
    auto headers = requestHeaders(key_, method, path, body, client_, device_, user_, nowMs());
    if (!token_.empty())
        headers["Authorization"] = "Bearer " + token_;
    for (const auto &[k, v] : headers)
        c.header(k, v);
    const std::string url = "https://api.nrd.nie.163.com" + path;
    std::string response;
    char error[CURL_ERROR_SIZE]{};
    curl_easy_setopt(c.handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c.handle, CURLOPT_HTTPHEADER, c.headers);
    curl_easy_setopt(c.handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (method != "GET") {
        curl_easy_setopt(c.handle, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(c.handle, CURLOPT_POSTFIELDSIZE, long(body.size()));
    }
    curl_easy_setopt(c.handle, CURLOPT_WRITEFUNCTION, append);
    curl_easy_setopt(c.handle, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(c.handle, CURLOPT_ERRORBUFFER, error);
    auto code = curl_easy_perform(c.handle);
    if (code != CURLE_OK)
        throw std::runtime_error(cancel_ ? "Canceled" : std::string("HTTPS: ") + curl_easy_strerror(code));
    long status = 0;
    curl_easy_getinfo(c.handle, CURLINFO_RESPONSE_CODE, &status);
    if (status == 204 || status == 205)
        return nullptr;
    auto json = Json::parse(response, nullptr, false);
    if (json.is_discarded() || !json.is_object())
        throw std::runtime_error("UU returned an invalid response (HTTP " + std::to_string(status) + ")");
    if (status == 401) {
        logout();
        throw std::runtime_error("登录已失效，请重新扫码");
    }
    if (status < 200 || status >= 300 || json.value("code", -1) != 0)
        throw std::runtime_error(json.value("msg", std::string("UU request failed")));
    return json.value("data", Json{});
}
void Api::initialize() {
    if (!device_.empty())
        return;
    auto d = request("POST", "/api/v1/device/android_tv/init",
                     {{"name", "UU远程 Switch 主控端"},
                      {"client_id", client_},
                      {"system_id", client_},
                      {"system_version", "Horizon"},
                      {"gaid", ""},
                      {"install_id", client_},
                      {"build_fingerprint", "uuremote-ns"},
                      {"brand", "Nintendo"},
                      {"manufacturer", "Nintendo"},
                      {"model", "Switch"},
                      {"product", "uuremote-ns"},
                      {"rom", ""},
                      {"abi", "arm64-v8a"},
                      {"resolution", "1280x720"},
                      {"screen_size", ""},
                      {"dpi", 96}});
    device_ = d.value("device_id", std::string{});
    if (device_.empty() && d.contains("current_device"))
        device_ = d["current_device"].value("device_id", std::string{});
    if (device_.empty())
        throw std::runtime_error("设备初始化未返回 ID");
}
void Api::login(const Json &result) {
    auto token = result.value("token", std::string{});
    if (token.empty())
        throw std::runtime_error("登录响应未返回令牌");
    token_ = std::move(token);
    const auto &u = result.at("user_id");
    user_ = u.is_string() ? u.get<std::string>() : u.dump();
}
SignalSocket::~SignalSocket() {
    close();
}
void SignalSocket::close() {
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
    received_.clear();
    parser_ = WsParser{};
}
void SignalSocket::write(const Bytes &data) {
    size_t p = 0;
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (p < data.size()) {
        if (cancel_)
            throw std::runtime_error("Canceled");
        size_t n = 0;
        auto code = curl_easy_send(curl_, data.data() + p, data.size() - p, &n);
        if (code == CURLE_AGAIN) {
            if (std::chrono::steady_clock::now() > end)
                throw std::runtime_error("Signaling write timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (code != CURLE_OK || !n)
            throw std::runtime_error("Signaling write failed");
        p += n;
    }
}
void SignalSocket::open(const std::string &url, const std::string &token) {
    close();
    if (token.find_first_of("\r\n") != std::string::npos)
        throw std::runtime_error("Invalid room token");
    curl_ = curl_easy_init();
    common(curl_, cancel_, 15000);
    auto https = "https://" + url.substr(6);
    curl_easy_setopt(curl_, CURLOPT_URL, https.c_str());
    curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 1L);
    auto code = curl_easy_perform(curl_);
    if (code != CURLE_OK)
        throw std::runtime_error("无法连接 UU 信令服务器");
    auto slash = https.find('/', 8);
    auto host = https.substr(8, slash - 8), path = https.substr(slash);
    const auto random = randomBytes(16);
    const auto key = base64(random.data(), random.size());
    auto req = "GET " + path + " HTTP/1.1\r\nHost: " + host +
               "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
               "\r\nSec-WebSocket-Version: 13\r\nX-NRD-AUTH: " + token +
               "\r\nX-NRD-CONTROLLING: 0\r\nstreamer_version: V1.14.22\r\n\r\n";
    write(Bytes(req.begin(), req.end()));
    std::string response;
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (response.find("\r\n\r\n") == std::string::npos) {
        if (cancel_)
            throw std::runtime_error("Canceled");
        char buf[8192];
        size_t n = 0;
        code = curl_easy_recv(curl_, buf, sizeof(buf), &n);
        if (code == CURLE_AGAIN) {
            if (std::chrono::steady_clock::now() > end)
                throw std::runtime_error("WebSocket handshake timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (code != CURLE_OK || n == 0)
            throw std::runtime_error("WebSocket handshake closed");
        response.append(buf, n);
        if (response.size() > 32768)
            throw std::runtime_error("WebSocket header limit");
    }
    auto split = response.find("\r\n\r\n") + 4;
    std::string header = response.substr(0, split);
    auto challenge = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t digest[20];
    mbedtls_sha1(reinterpret_cast<const uint8_t *>(challenge.data()), challenge.size(), digest);
    // Header names are case-insensitive, field values remain exact.
    Headers parsed;
    size_t p = header.find("\r\n") + 2;
    while (p < header.size() - 2) {
        auto e = header.find("\r\n", p), c = header.find(':', p);
        if (c >= e)
            throw std::runtime_error("Malformed upgrade headers");
        auto name = header.substr(p, c - p);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char ch) { return std::tolower(ch); });
        auto value = header.substr(c + 1, e - c - 1);
        while (!value.empty() && value.front() == ' ')
            value.erase(0, 1);
        while (!value.empty() && value.back() == ' ')
            value.pop_back();
        parsed[name] = value;
        p = e + 2;
    }
    if (header.rfind("HTTP/1.1 101 ", 0) != 0 || parsed["sec-websocket-accept"] != base64(digest, 20))
        throw std::runtime_error("WebSocket upgrade rejected");
    if (response.size() > split)
        for (auto &m : parser_.feed(reinterpret_cast<const uint8_t *>(response.data() + split),
                                    response.size() - split))
            received_.push_back(std::move(m));
}
void SignalSocket::send(const std::string &text) {
    Bytes b(text.begin(), text.end());
    auto mask = randomBytes(4);
    write(wsFrame(1, b, {mask[0], mask[1], mask[2], mask[3]}));
}
void SignalSocket::sendBinary(const Bytes &bytes) {
    if (bytes.empty())
        return;
    auto mask = randomBytes(4);
    write(wsFrame(2, bytes, {mask[0], mask[1], mask[2], mask[3]}));
}
bool SignalSocket::receive(WsMessage &message) {
    if (received_.empty()) {
        uint8_t buf[16384];
        size_t n = 0;
        auto code = curl_easy_recv(curl_, buf, sizeof(buf), &n);
        if (code == CURLE_AGAIN)
            return false;
        if (code != CURLE_OK || n == 0)
            throw std::runtime_error("信令连接已断开");
        for (auto &m : parser_.feed(buf, n))
            received_.push_back(std::move(m));
    }
    while (!received_.empty()) {
        auto m = std::move(received_.front());
        received_.pop_front();
        if (m.opcode == 8)
            throw std::runtime_error("远端已关闭信令连接");
        if (m.opcode == 9) {
            auto mask = randomBytes(4);
            write(wsFrame(10, m.data, {mask[0], mask[1], mask[2], mask[3]}));
            continue;
        }
        if (m.opcode == 10)
            continue;
        message = std::move(m);
        return true;
    }
    return false;
}
} // namespace uu
