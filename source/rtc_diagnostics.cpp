#include "rtc_diagnostics.hpp"
#include "diagnostics.hpp"

namespace uu {
std::string rtcLogEvent(std::string_view message) {
    const auto delimiter = message.find(": "), function = message.find('@');
    if (function != message.npos && delimiter != message.npos && function < delimiter)
        message.remove_prefix(delimiter + 2);
    if (message.substr(0, 11) == "DTLS recv: ")
        message.remove_prefix(11);
    struct Rule {
        const char *prefix, *event;
    };
    static constexpr Rule rules[] = {
        {"DTLS handshake finished", "rtc-dtls-connected"},
        {"DTLS handshake failed", "rtc-dtls-failed"},
        {"DTLS closed", "rtc-dtls-closed"},
        {"Handshake failed", "rtc-dtls-handshake-error"},
        {"Invalid fingerprint", "rtc-dtls-fingerprint-error"},
        {"Failed to get SRTP profile", "rtc-srtp-profile-error"},
        {"Failed to derive SRTP keys", "rtc-srtp-key-error"},
        {"SCTP connected", "rtc-sctp-connected"},
        {"SCTP connection failed", "rtc-sctp-failed"},
        {"SCTP disconnected", "rtc-sctp-disconnected"},
        {"Could not bind usrsctp socket", "rtc-sctp-bind-error"},
        {"Connection attempt failed", "rtc-sctp-connect-error"},
        {"Could not create SCTP socket", "rtc-sctp-socket-error"},
        {"Could not set socket option", "rtc-sctp-option-error"},
        {"Could not subscribe to event", "rtc-sctp-event-error"},
        {"SCTP sending failed", "rtc-sctp-send-error"},
        {"SCTP transport initialization failed", "rtc-sctp-init-error"},
        {"DTLS transport initialization failed", "rtc-dtls-init-error"},
        {"ICE transport initialization failed", "rtc-ice-init-error"},
        {"juice: TURN address resolution failed", "rtc-turn-dns-error"},
        {"juice: STUN server address resolution failed", "rtc-stun-dns-error"},
        {"juice: No local host candidates gathered", "rtc-ice-no-local-candidate"},
        {"连接超时", "failure-connect-timeout"},
        {"信令心跳超时", "failure-signal-heartbeat"},
        {"信令连接已断开", "failure-signal-read"},
        {"远端已关闭信令连接", "failure-signal-remote-close"},
        {"UU 已关闭连接", "failure-signal-server-close"},
        {"UU 信令拒绝连接", "failure-signal-rejected"},
        {"远控权限已释放或电脑已退出房间", "failure-control-released"},
        {"Signaling write timeout", "failure-signal-write-timeout"},
        {"Signaling write failed", "failure-signal-write"},
        {"Unexpected signaling attachment", "failure-signal-attachment"},
        {"Interleaved signaling attachment", "failure-signal-interleaved"},
        {"Invalid signaling event", "failure-signal-event"},
        {"远端 ICE 候选过多", "failure-candidate-limit"},
        {"Got a remote candidate without remote description", "failure-candidate-no-sdp"},
        {"Got a remote candidate without ICE transport", "failure-candidate-no-ice"},
        {"Failed to gather local ICE candidates", "failure-candidate-gathering"},
        {"音视频连接已中断", "failure-peer-disconnected"},
        {"ICE 网络连接失败", "failure-ice"},
        {"加密或数据通道建立失败", "failure-dtls-or-sctp"},
    };
    for (const auto &rule : rules) {
        const std::string_view prefix(rule.prefix);
        if (message.substr(0, prefix.size()) != prefix)
            continue;
        std::string event(rule.event);
        for (const auto marker : {std::string_view("errno="), std::string_view("[code=")}) {
            const auto at = message.find(marker);
            if (at == message.npos)
                continue;
            size_t begin = at + marker.size(), end = begin;
            if (end < message.size() && message[end] == '-')
                ++end;
            const auto digits = end;
            while (end < message.size() && end - digits < 6 && message[end] >= '0' && message[end] <= '9')
                ++end;
            if (end > digits && (end == message.size() || message[end] < '0' || message[end] > '9'))
                event += "-code-" + std::string(message.substr(begin, end - begin));
        }
        return event;
    }
    return {};
}
std::string_view sctpHandshakeEvent(bool outgoing, const uint8_t *data, size_t size) noexcept {
    if (!data || size < 16)
        return {};
    switch (data[12]) {
    case 1:
        return outgoing ? "rtc-sctp-tx-init" : "rtc-sctp-rx-init";
    case 2:
        return outgoing ? "rtc-sctp-tx-init-ack" : "rtc-sctp-rx-init-ack";
    case 6:
        return outgoing ? "rtc-sctp-tx-abort" : "rtc-sctp-rx-abort";
    case 10:
        return outgoing ? "rtc-sctp-tx-cookie-echo" : "rtc-sctp-rx-cookie-echo";
    case 11:
        return outgoing ? "rtc-sctp-tx-cookie-ack" : "rtc-sctp-rx-cookie-ack";
    default:
        return {};
    }
}
void diagnosticSctpHandshake(bool outgoing, const uint8_t *data, size_t size) noexcept {
    const auto event = sctpHandshakeEvent(outgoing, data, size);
    if (!event.empty())
        diagnostic(event.data());
}
} // namespace uu
