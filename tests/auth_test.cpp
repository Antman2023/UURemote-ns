#include "auth.hpp"
#include <iostream>
#include <stdexcept>
using namespace uu;
void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
template <class Function> void rejects(Function function) {
    bool rejected = false;
    try {
        function();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Expected failure");
}
int main() {
    try {
        Json input = {{"countryCode", " +86 "}, {"mobile", "138-0000 0000"}, {"code", " 012345 "}};
        check(smsCredentials(input, true) ==
                  Json({{"country_code", "86"}, {"mobile", "13800000000"}, {"code", "012345"}}),
              "SMS credential wire format");
        check(smsCredentials({{"countryCode", "+1"}, {"mobile", "2025550123"}})["country_code"] == "1",
              "International country code");
        for (const auto &value : Json::array({Json::array(), Json{{"mobile", 12345}}, Json{{"mobile", "123"}},
                                              Json{{"countryCode", "0"}, {"mobile", "13800000000"}},
                                              Json{{"mobile", "12800000000"}}}))
            rejects([&] { smsCredentials(value); });
        for (const auto *value : {"12345", "1234567", "12a456", ""})
            rejects([&] { smsCredentials({{"mobile", "13800000000"}, {"code", value}}, true); });
        SmsAuth sms;
        int calls = 0;
        Json sent;
        auto send = [&](const std::string &path, const Json &body) -> Json {
            ++calls;
            check(path == "/api/v1/security/mobile/code", "Send SMS endpoint");
            sent = body;
            return nullptr;
        };
        sms.sendCode(send, input, 1000);
        check(calls == 1 &&
                  sent == Json({{"country_code", "86"}, {"mobile", "13800000000"}, {"type", "login"}}),
              "Send SMS request");
        rejects([&] { sms.sendCode(send, input, 60999); });
        check(calls == 1, "Cooldown must prevent a network request");
        sms.sendCode(send, input, sms.retryAt());
        check(calls == 2, "Cooldown expiry");
        SmsAuth failed;
        rejects([&] {
            failed.sendCode(
                [](const std::string &, const Json &) -> Json { throw std::runtime_error("No network"); },
                input, 1000);
        });
        check(failed.retryAt() == 0, "Failure must not start cooldown");
        auto result = sms.login(
            [&](const std::string &path, const Json &body) -> Json {
                check(path == "/api/v1/login/by_mobile", "SMS login endpoint");
                check(body["code"] == "012345" && !body.contains("type"), "Code preserves leading zero");
                return {{"token", "test-token"}, {"user_id", "test-user"}, {"nickname", nullptr}};
            },
            input);
        check(loginAccount(result).nickname == "UU 用户", "Nullable nickname");
        rejects([&] {
            sms.login([](const std::string &, const Json &) -> Json { return {{"token", "incomplete"}}; },
                      input);
        });
        rejects([] { loginAccount({{"token", "test"}, {"user_id", nullptr}}); });
        check(loginAccount({{"token", "test"}, {"user_id", 42}}).userId == "42", "Numeric user identifier");
        check(roomSignalUrl({{"signaling_server", nullptr},
                             {"signaling_list", Json::array({nullptr, "signal.nie.163.com"})}}) ==
                  "wss://signal.nie.163.com/socket.io/?EIO=4&transport=websocket",
              "Nullable room signaling fallback");
        rejects([] { roomSignalUrl({{"signaling_server", nullptr}}); });
        for (int modules = 21; modules <= 177; modules += 4) {
            auto qr = qrGeometry(modules);
            check(qr.size == (modules + 8) * qr.scale && qr.border == 4, "Integer QR modules and quiet zone");
            check(qr.x >= 640 && qr.x + qr.size <= 1218 && qr.y >= 140 && qr.y + qr.size <= 648,
                  "QR fits scan area");
        }
        check(qrGeometry(49).size >= 440, "Typical login QR is substantially larger");
        std::cout << "Auth tests passed: SMS payloads, cooldown, failures, login validation, room fallback "
                     "and QR layout.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
