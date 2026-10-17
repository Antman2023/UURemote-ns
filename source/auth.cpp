#include "auth.hpp"
#include <cctype>
#include <chrono>
#include <stdexcept>

namespace uu {
static std::string trim(std::string value) {
    auto space = [](unsigned char c) { return std::isspace(c); };
    while (!value.empty() && space(value.front()))
        value.erase(value.begin());
    while (!value.empty() && space(value.back()))
        value.pop_back();
    return value;
}
static bool digits(const std::string &text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
}
Json smsCredentials(const Json &input, bool requireCode) {
    if (!input.is_object())
        throw std::runtime_error("短信登录参数无效");
    std::string country = "86", mobile;
    if (input.contains("countryCode")) {
        if (!input["countryCode"].is_string())
            throw std::runtime_error("请输入有效的国家或地区区号");
        country = trim(input["countryCode"].get<std::string>());
        if (!country.empty() && country.front() == '+')
            country.erase(country.begin());
    }
    if (!digits(country) || country.size() > 4 || country.front() == '0')
        throw std::runtime_error("请输入有效的国家或地区区号");
    if (input.contains("mobile") && input["mobile"].is_string())
        mobile = input["mobile"].get<std::string>();
    mobile.erase(std::remove_if(mobile.begin(), mobile.end(),
                                [](unsigned char c) { return std::isspace(c) || c == '-'; }),
                 mobile.end());
    if (!digits(mobile) || mobile.size() < 4 || mobile.size() > 15 ||
        (country == "86" && (mobile.size() != 11 || mobile[0] != '1' || mobile[1] < '3' || mobile[1] > '9')))
        throw std::runtime_error("请输入有效的手机号码");
    Json result = {{"country_code", country}, {"mobile", mobile}};
    if (requireCode) {
        std::string code;
        if (input.contains("code") && input["code"].is_string())
            code = trim(input["code"].get<std::string>());
        if (!digits(code) || code.size() != 6)
            throw std::runtime_error("请输入 6 位短信验证码");
        result["code"] = code;
    }
    return result;
}
LoginAccount loginAccount(const Json &response) {
    if (!response.is_object() || !response.contains("token") || !response["token"].is_string() ||
        response["token"].get_ref<const std::string &>().empty() || !response.contains("user_id") ||
        (!response["user_id"].is_string() && !response["user_id"].is_number()))
        throw std::runtime_error("登录响应缺少账号信息");
    LoginAccount result;
    result.token = response["token"].get<std::string>();
    result.userId =
        response["user_id"].is_string() ? response["user_id"].get<std::string>() : response["user_id"].dump();
    if (result.userId.empty())
        throw std::runtime_error("登录响应缺少账号信息");
    result.nickname = response.contains("nickname") && response["nickname"].is_string()
                          ? response["nickname"].get<std::string>()
                          : "UU 用户";
    return result;
}
void SmsAuth::sendCode(const Request &request, const Json &input, int64_t now) {
    auto body = smsCredentials(input);
    if (now < retryAt_)
        throw std::runtime_error("验证码已发送，请等待倒计时结束后重发");
    body["type"] = "login";
    const auto started = std::chrono::steady_clock::now();
    request("/api/v1/security/mobile/code", body);
    retryAt_ =
        now + 60000 +
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
            .count();
}
Json SmsAuth::login(const Request &request, const Json &input) {
    auto result = request("/api/v1/login/by_mobile", smsCredentials(input, true));
    loginAccount(result);
    return result;
}
QrGeometry qrGeometry(int modules) {
    if (modules < 21 || modules > 177)
        throw std::runtime_error("二维码尺寸无效");
    const int scale = 480 / (modules + 8), size = (modules + 8) * scale;
    return {920 - size / 2, 158, size, scale, 4};
}
std::string roomSignalUrl(const Json &room) {
    if (!room.is_object())
        throw std::runtime_error("加入房间响应无效");
    std::string address;
    if (room.contains("signaling_list") && room["signaling_list"].is_array())
        for (const auto &entry : room["signaling_list"])
            if (entry.is_string() && !entry.get_ref<const std::string &>().empty()) {
                address = entry.get<std::string>();
                break;
            }
    if (address.empty() && room.contains("signaling_server") && room["signaling_server"].is_string())
        address = room["signaling_server"].get<std::string>();
    if (address.empty())
        throw std::runtime_error("加入房间未返回信令服务器");
    return signalingUrl(address);
}
} // namespace uu
