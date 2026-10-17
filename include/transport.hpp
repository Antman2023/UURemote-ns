#pragma once
#include "protocol.hpp"
#include <atomic>
#include <curl/curl.h>
#include <deque>

namespace uu {
class Api {
    std::string key_, client_, device_, token_, user_;
    std::atomic<bool> &cancel_;

  public:
    Api(std::string key, std::string client, std::atomic<bool> &cancel)
        : key_(std::move(key)), client_(std::move(client)), cancel_(cancel) {}
    Json request(const std::string &method, const std::string &path, const Json &data = nullptr,
                 int timeout = 20000);
    void initialize();
    void login(const Json &result);
    void logout() {
        token_.clear();
        user_.clear();
    }
    bool authenticated() const {
        return !token_.empty();
    }
    const std::string &device() const {
        return device_;
    }
};
class SignalSocket {
    CURL *curl_ = nullptr;
    std::atomic<bool> &cancel_;
    WsParser parser_;
    std::deque<WsMessage> received_;
    void write(const Bytes &data);

  public:
    explicit SignalSocket(std::atomic<bool> &cancel) : cancel_(cancel) {}
    ~SignalSocket();
    void open(const std::string &url, const std::string &token);
    void send(const std::string &text);
    void sendBinary(const Bytes &bytes);
    bool receive(WsMessage &message);
    void close();
};
} // namespace uu
