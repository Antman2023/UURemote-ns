#pragma once
#include "protocol.hpp"
#include <functional>

namespace uu {
struct LoginAccount {
    std::string token, userId, nickname;
};
LoginAccount loginAccount(const Json &response);
Json smsCredentials(const Json &input, bool requireCode = false);
class SmsAuth {
  public:
    using Request = std::function<Json(const std::string &, const Json &)>;
    int64_t retryAt() const {
        return retryAt_;
    }
    void sendCode(const Request &request, const Json &input, int64_t now);
    Json login(const Request &request, const Json &input);

  private:
    int64_t retryAt_ = 0;
};
struct QrGeometry {
    int x, y, size, scale, border = 4;
};
QrGeometry qrGeometry(int modules);
std::string roomSignalUrl(const Json &room);
} // namespace uu
