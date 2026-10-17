#pragma once
#include "media.hpp"
#include "transport.hpp"
#include <functional>
#include <rtc/rtc.hpp>
#include <set>

namespace uu {
struct Snapshot {
    std::string stage = "idle", detail = "按 A 生成二维码，使用 UU远程 App 扫码", qrUrl, nickname;
    Json devices = Json::array();
    bool authenticated = false, streaming = false;
    int width = 1280, height = 720, fps = 30;
};
class Client {
    struct Event {
        std::string type;
        Json data;
        uint64_t generation = 0;
    };
    struct Inbox {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<Event> events;
    };
    std::shared_ptr<Inbox> inbox_ = std::make_shared<Inbox>();
    std::shared_ptr<Media> media_;
    std::atomic<bool> stop_{false}, cancel_{false};
    std::thread worker_;
    std::mutex stateMutex_;
    Snapshot state_;
    std::unique_ptr<Api> api_;
    std::unique_ptr<SignalSocket> socket_;
    std::shared_ptr<rtc::PeerConnection> peer_;
    std::map<std::string, std::shared_ptr<rtc::DataChannel>> channels_;
    std::shared_ptr<rtc::Track> video_, audio_;
    Json remote_, ice_;
    std::string control_;
    uint64_t generation_ = 1;
    int nextAck_ = 1;
    std::map<int, std::string> acks_;
    std::set<int> heldKeys_, heldButtons_;
    std::deque<Json> earlySoac_;
    SioPacket pending_;
    int remainingAttachments_ = 0;
    int64_t deadline_ = 0, lastPacket_ = 0, heartbeatTimeout_ = 45000, qrExpires_ = 0, qrNext_ = 0;
    Json qr_;
    int qrStatus_ = 1;
    void run();
    void handle(const Event &event);
    void packet(const WsMessage &message);
    void dispatch(const SioPacket &packet);
    void beginPeer(const Json &control);
    void handleSoac(const Json &data);
    void emit(const std::string &name, const Json &data, const std::string &ack = "",
              const Bytes &attachment = {});
    void setState(const std::string &stage, const std::string &detail);
    void refresh();
    void disconnect();
    void input(const Json &data);
    void release();
    void pollQr();
    void post(std::string type, Json data = nullptr, uint64_t generation = 0);

  public:
    Client(std::string key, std::string client, std::shared_ptr<Media> media);
    ~Client();
    Snapshot snapshot();
    void command(const std::string &name, Json data = nullptr);
};
} // namespace uu
