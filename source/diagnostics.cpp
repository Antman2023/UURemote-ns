#include "diagnostics.hpp"
#include <cstdio>
#include <ctime>
#include <mutex>

namespace uu {
const char *diagnosticPath() noexcept {
#ifdef __SWITCH__
    return "sdmc:/switch/UURemote-ns/connection.log";
#else
    return "connection.log";
#endif
}
void diagnostic(const char *event) noexcept {
    // Callers supply fixed stage names only; never pass API data, SDP, phone or credentials.
    try {
        static std::mutex mutex;
        std::lock_guard lock(mutex);
        auto *file = std::fopen(diagnosticPath(), "a");
        if (!file)
            return;
        std::fseek(file, 0, SEEK_END);
        if (std::ftell(file) > 128 * 1024) {
            std::fclose(file);
            file = std::fopen(diagnosticPath(), "w");
            if (!file)
                return;
        }
        std::fprintf(file, "%lld %s\n", static_cast<long long>(std::time(nullptr)), event);
        std::fflush(file);
        std::fclose(file);
    } catch (...) {
    }
}
std::string connectionFailure(const std::string &stage, const std::string &error) {
    std::string name;
    if (stage == "joining")
        name = "加入房间";
    else if (stage == "signaling")
        name = "信令连接";
    else if (stage == "negotiating")
        name = "申请控制权";
    else if (stage == "rtc-init")
        name = "初始化串流";
    else if (stage == "connecting")
        name = "音视频协商";
    else if (stage == "connected")
        name = "远程会话";
    return name.empty() ? error : name + "失败：" + error;
}
} // namespace uu
