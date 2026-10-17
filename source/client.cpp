#include "client.hpp"
#include "diagnostics.hpp"
#include <stdexcept>

namespace uu {
Client::Client(std::string key, std::string client, std::shared_ptr<Media> media) : media_(std::move(media)) {
    api_ = std::make_unique<Api>(std::move(key), std::move(client), cancel_);
    worker_ = std::thread([this] { run(); });
}
Client::~Client() {
    stop_ = true;
    cancel_ = true;
    inbox_->cv.notify_all();
    worker_.join();
}
Snapshot Client::snapshot() {
    std::lock_guard lock(stateMutex_);
    return state_;
}
void Client::setState(const std::string &stage, const std::string &detail) {
    diagnostic(stage.c_str());
    std::lock_guard lock(stateMutex_);
    state_.stage = stage;
    state_.detail = detail;
    state_.streaming = stage == "connected";
    state_.authenticated = api_->authenticated();
}
void Client::post(std::string type, Json data, uint64_t generation) {
    std::lock_guard lock(inbox_->mutex);
    inbox_->events.push_back({std::move(type), std::move(data), generation});
    inbox_->cv.notify_one();
}
void Client::command(const std::string &name, Json data) {
    if (name == "disconnect" || name == "logout" || name == "cancel" || name == "sms-open" || name == "login")
        cancel_ = true;
    post(name, std::move(data));
}
void Client::run() {
    while (!stop_) {
        try {
            std::deque<Event> events;
            {
                std::unique_lock lock(inbox_->mutex);
                if (inbox_->events.empty())
                    inbox_->cv.wait_for(lock, std::chrono::milliseconds(4));
                events.swap(inbox_->events);
            }
            for (const auto &e : events) {
                if (stop_)
                    break;
                if (e.generation && e.generation != generation_)
                    continue;
                handle(e);
            }
            if (stop_)
                break;
            if (socket_) {
                WsMessage m;
                int count = 0;
                while (socket_->receive(m) && count++ < 64)
                    packet(m);
                if (nowMs() - lastPacket_ > heartbeatTimeout_)
                    throw std::runtime_error("信令心跳超时");
            }
            if (deadline_ && nowMs() > deadline_)
                throw std::runtime_error("连接超时，请检查电脑和网络后重试");
            if (!qr_.is_null() && nowMs() >= qrNext_)
                pollQr();
        } catch (const std::exception &error) {
            if (stop_)
                break;
            const auto failure = connectionFailure(snapshot().stage, error.what());
            diagnostic("client-exception");
            disconnect();
            if (cancel_) {
                cancel_ = false;
                setState("idle", "已取消");
            } else
                setState("error", failure);
        } catch (...) {
            diagnostic("client-unknown-exception");
            disconnect();
            cancel_ = false;
            setState("error", "发生异常，连接阶段已写入 connection.log");
        }
    }
    cancel_ = false;
    disconnect();
}
void Client::handle(const Event &event) {
    const auto &e = event.type;
    const auto &data = event.data;
    if (e == "login") {
        disconnect();
        cancel_ = false;
        {
            std::lock_guard lock(stateMutex_);
            state_.loginMode = "qr";
        }
        setState("login", "正在生成登录二维码…");
        api_->initialize();
        qr_ = api_->request("POST", "/api/v1/qrcode/gen/login");
        if (!qr_.is_object() || !qr_.contains("token") || !qr_.contains("qrcode_jump_url"))
            throw std::runtime_error("登录二维码响应不完整");
        qrExpires_ = nowMs() + 180000;
        qrNext_ = nowMs() + 1000;
        qrStatus_ = 1;
        {
            std::lock_guard lock(stateMutex_);
            state_.qrUrl = qr_["qrcode_jump_url"].get<std::string>();
        }
        setState("login", "请使用手机 UU远程 App 扫码并确认");
    } else if (e == "sms-open") {
        disconnect();
        cancel_ = false;
        {
            std::lock_guard lock(stateMutex_);
            state_.loginMode = "sms";
        }
        setState("sms", "输入手机号并发送验证码");
    } else if (e == "sms-code" || e == "sms-login") {
        cancel_ = false;
        auto request = [this](const std::string &path, const Json &body) {
            api_->initialize();
            return api_->request("POST", path, body);
        };
        if (e == "sms-code") {
            setState("sms-sending", "正在发送短信验证码…");
            sms_.sendCode(request, data, nowMs());
            {
                std::lock_guard lock(stateMutex_);
                state_.smsRetryAt = sms_.retryAt();
            }
            setState("sms", "验证码已发送，输入 6 位验证码后登录");
        } else {
            setState("sms-verifying", "正在验证短信验证码…");
            completeLogin(sms_.login(request, data));
        }
    } else if (e == "cancel" || e == "disconnect") {
        disconnect();
        cancel_ = false;
        {
            std::lock_guard lock(stateMutex_);
            state_.loginMode = "qr";
        }
        setState("idle", "已断开，按 Y 刷新设备或按 A 登录");
    } else if (e == "logout") {
        disconnect();
        cancel_ = false;
        api_->logout();
        {
            std::lock_guard lock(stateMutex_);
            state_.devices = Json::array();
            state_.nickname.clear();
        }
        setState("idle", "已退出账号，按 A 重新扫码");
    } else if (e == "refresh") {
        if (api_->authenticated()) {
            cancel_ = false;
            setState("loading", "正在同步设备…");
            refresh();
        } else
            setState("idle", "请先按 A 扫码登录");
    } else if (e == "connect") {
        disconnect();
        cancel_ = false;
        setState("joining", "正在加入远控房间…");
        auto room = api_->request(
            "POST", "/api/v1/room/join/by_device/" + percentEncode(data.at("deviceId").get<std::string>()),
            {{"force_join", false}});
        auto url = roomSignalUrl(room);
        if (!room.contains("token") || !room["token"].is_string() ||
            room["token"].get_ref<const std::string &>().empty())
            throw std::runtime_error("加入房间未返回信令凭据");
        socket_ = std::make_unique<SignalSocket>(cancel_);
        setState("signaling", "正在连接 UU 信令服务器…");
        socket_->open(url, room.at("token").get<std::string>());
        lastPacket_ = nowMs();
        deadline_ = nowMs() + 45000;
    } else if (e == "input") {
        input(data);
    } else if (e == "release") {
        release();
    } else if (e == "local") {
        emit("soac",
             soac(remote_, data.at("type").get<std::string>(), {{"sdp", data.at("sdp")}}, ice_, control_));
    } else if (e == "candidate") {
        emit("soac", soac(remote_, "candidate", data, ice_, control_));
    } else if (e == "peer-state") {
        auto s = data.get<int>();
        if (s == int(rtc::PeerConnection::State::Connected)) {
            deadline_ = 0;
            setState("connected", "已连接 · 左摇杆移动鼠标 · ZR/ZL 点击 · − 断开");
        } else if (s == int(rtc::PeerConnection::State::Failed) ||
                   s == int(rtc::PeerConnection::State::Disconnected) ||
                   s == int(rtc::PeerConnection::State::Closed))
            throw std::runtime_error("音视频连接已中断，请重新连接");
    } else if (e == "async-error")
        throw std::runtime_error(data.get<std::string>());
}
void Client::pollQr() {
    if (nowMs() > qrExpires_) {
        qr_ = nullptr;
        {
            std::lock_guard lock(stateMutex_);
            state_.qrUrl.clear();
        }
        setState("idle", "二维码已过期，按 A 重新生成");
        return;
    }
    Json data = qr_;
    data["login_status"] = qrStatus_;
    auto result = api_->request("POST", "/api/v1/qrcode/login/status", data, 65000);
    qrStatus_ = result.value("login_status", 1);
    qrNext_ = nowMs() + 1000;
    if (qrStatus_ == 4) {
        auto login = api_->request("POST", "/api/v1/login/by_qrcode", qr_);
        completeLogin(login);
    } else if (qrStatus_ == 3) {
        qr_ = nullptr;
        setState("idle", "手机已取消登录，按 A 重试");
    } else if (qrStatus_ == 2)
        setState("login", "扫码成功，请在手机上确认登录");
}
void Client::completeLogin(const Json &response) {
    if (cancel_)
        throw std::runtime_error("Canceled");
    auto account = loginAccount(response);
    api_->login(response);
    qr_ = nullptr;
    {
        std::lock_guard lock(stateMutex_);
        state_.qrUrl.clear();
        state_.nickname = account.nickname;
        state_.loginMode = "qr";
    }
    setState("loading", "登录成功，正在同步设备…");
    refresh();
}
void Client::refresh() {
    auto response = api_->request("GET", "/api/v1/device/list");
    Json devices = Json::array();
    std::set<std::string> seen;
    for (auto name : {"my_binded_devices", "others_shared_devices"})
        if (response.contains(name) && response[name].is_array())
            for (auto d : response[name]) {
                auto id = d.value("device_id", std::string{});
                if (id.empty() || id == api_->device() || !seen.insert(id).second)
                    continue;
                devices.push_back(std::move(d));
            }
    bool empty = devices.empty();
    {
        std::lock_guard lock(stateMutex_);
        state_.devices = std::move(devices);
    }
    setState("idle", empty ? "暂无电脑，请在原版 UU 电脑端登录同一账号"
                           : "选择电脑，按 A 连接 · Y 刷新 · X 退出账号");
}
void Client::emit(const std::string &name, const Json &data, const std::string &ack,
                  const Bytes &attachment) {
    if (!socket_)
        return;
    Json args = Json::array({name});
    if (!data.is_null())
        args.push_back(data);
    std::string id;
    if (!ack.empty()) {
        int n = nextAck_++;
        acks_[n] = ack;
        id = std::to_string(n);
    }
    socket_->send((attachment.empty() ? "42" : "451-") + id + args.dump());
    if (!attachment.empty())
        socket_->sendBinary(attachment);
}
void Client::packet(const WsMessage &message) {
    lastPacket_ = nowMs();
    if (message.opcode == 2) {
        if (remainingAttachments_ <= 0)
            throw std::runtime_error("Unexpected signaling attachment");
        if (--remainingAttachments_ == 0)
            dispatch(pending_);
        return;
    }
    std::string text(message.data.begin(), message.data.end());
    if (text.empty())
        return;
    if (text[0] == '0') {
        auto hello = Json::parse(text.substr(1));
        heartbeatTimeout_ = hello.value("pingInterval", 25000) + hello.value("pingTimeout", 20000) + 5000;
        socket_->send("40");
        return;
    }
    if (text[0] == '2') {
        socket_->send("3" + text.substr(1));
        return;
    }
    if (text[0] == '1')
        throw std::runtime_error("UU 已关闭连接");
    if (text[0] != '4')
        return;
    if (remainingAttachments_)
        throw std::runtime_error("Interleaved signaling attachment");
    auto p = parseSocketIo(text);
    if (p.attachments) {
        pending_ = p;
        remainingAttachments_ = p.attachments;
    } else
        dispatch(p);
}
void Client::dispatch(const SioPacket &p) {
    if (p.type == 0) {
        emit("room_info", nullptr, "room");
        return;
    }
    if (p.type == 1 || p.type == 4)
        throw std::runtime_error("UU 信令拒绝连接");
    if (p.type == 3 || p.type == 6) {
        auto it = acks_.find(p.id);
        if (it == acks_.end())
            return;
        auto event = it->second;
        acks_.erase(it);
        auto value = ackValue(p.data, event);
        if (event == "room") {
            auto publisher = value.at("publisher");
            remote_ = publisher.is_object() ? publisher.value("client_id", publisher.value("id", Json{}))
                                            : publisher;
            if ((!remote_.is_string() && !remote_.is_number()) || remote_ == "")
                throw std::runtime_error("房间内没有在线电脑");
            control_ = uuid();
            auto options = connectOptions();
            options.insert(options.begin(), 4);
            setState("negotiating", "正在申请远控权限…");
            // Preserve room ICE configuration if control does not repeat it.
            ice_ = value.value("iceServers", Json::array());
            emit("control",
                 {{"app_data", {{"_placeholder", true}, {"num", 0}}},
                  {"streamer_data", ""},
                  {"app_control_id", control_}},
                 "control", options);
        } else if (event == "control")
            beginPeer(value);
    } else if (p.type == 2 || p.type == 5) {
        if (!p.data.is_array() || p.data.empty() || !p.data[0].is_string())
            throw std::runtime_error("Invalid signaling event");
        auto event = p.data[0].get<std::string>();
        if (event == "soac" && p.data.size() > 1) {
            if (peer_)
                handleSoac(p.data[1]);
            else if (earlySoac_.size() < 128)
                earlySoac_.push_back(p.data[1]);
        } else if (event == "released" || event == "left")
            throw std::runtime_error("远控权限已释放或电脑已退出房间");
        if (p.id >= 0)
            socket_->send("43" + std::to_string(p.id) + "[]");
    }
}
void Client::beginPeer(const Json &result) {
    auto servers = result.value("iceServers", ice_);
    remote_ = result.value("client_id", remote_);
    ice_ = result.at("ice_id");
    rtc::Configuration config;
    config.disableAutoNegotiation = true;
    if (result.value("force_relay", false))
        config.iceTransportPolicy = rtc::TransportPolicy::Relay;
    for (const auto &s : servers) {
        auto urls = s.value("urls", s.value("url", Json{}));
        if (urls.is_string())
            urls = Json::array({urls});
        if (!urls.is_array())
            continue;
        for (const auto &u : urls) {
            rtc::IceServer server(u.get<std::string>());
            server.username = s.value("username", std::string{});
            server.password = s.value("credential", s.value("password", std::string{}));
            config.iceServers.push_back(std::move(server));
        }
    }
    setState("rtc-init", "正在初始化原生串流…");
    diagnostic("rtc-runtime");
    rtc::Preload();
    diagnostic("rtc-peer");
    peer_ = std::make_shared<rtc::PeerConnection>(config);
    auto weak = std::weak_ptr<Inbox>(inbox_);
    auto gen = generation_;
    auto post = [weak, gen](std::string type, Json data) {
        if (auto box = weak.lock()) {
            std::lock_guard lock(box->mutex);
            if (box->events.size() < 2048)
                box->events.push_back({std::move(type), std::move(data), gen});
            box->cv.notify_one();
        }
    };
    peer_->onLocalDescription([post](rtc::Description desc) {
        post("local", {{"type", desc.typeString()}, {"sdp", std::string(desc)}});
    });
    peer_->onLocalCandidate([post](rtc::Candidate c) {
        post("candidate", {{"candidate", std::string(c)}, {"sdpMid", c.mid()}, {"sdpMLineIndex", 0}});
    });
    peer_->onStateChange([post](rtc::PeerConnection::State s) { post("peer-state", int(s)); });
    rtc::Description::Video v("0", rtc::Description::Direction::RecvOnly);
    diagnostic("rtc-video");
    v.addH264Codec(96, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
    v.setBitrate(5000);
    video_ = peer_->addTrack(v);
    auto depacketizer = std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::StartSequence);
    depacketizer->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
    video_->setMediaHandler(depacketizer);
    std::weak_ptr<Media> media = media_;
    auto mediaGeneration = media_->generation();
    video_->onFrame([media, mediaGeneration](rtc::binary data, rtc::FrameInfo) {
        if (auto m = media.lock())
            m->video(reinterpret_cast<const uint8_t *>(data.data()), data.size(), mediaGeneration);
    });
    rtc::Description::Audio a("1", rtc::Description::Direction::RecvOnly);
    diagnostic("rtc-audio");
    a.addOpusCodec(111);
    audio_ = peer_->addTrack(a);
    auto opus = std::make_shared<rtc::OpusRtpDepacketizer>();
    opus->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
    audio_->setMediaHandler(opus);
    audio_->onFrame([media, mediaGeneration](rtc::binary data, rtc::FrameInfo) {
        if (auto m = media.lock())
            m->audio(reinterpret_cast<const uint8_t *>(data.data()), data.size(), mediaGeneration);
    });
    diagnostic("rtc-channels");
    for (const auto *label : {"CONTROL_DATA_CHANNEL", "TEXT_DATA_CHANNEL", "STREAMER_DATA_CHANNEL",
                              "FILE_DATA_CHANNEL", "BINARY_DATA_CHANNEL"})
        channels_[label] = peer_->createDataChannel(label);
    setState("connecting", "正在建立音视频通道…");
    diagnostic("rtc-offer");
    peer_->setLocalDescription();
    for (const auto &e : earlySoac_)
        handleSoac(e);
    earlySoac_.clear();
}
void Client::handleSoac(const Json &packet) {
    if (!packet.contains("data") || !packet["data"].is_object())
        return;
    const auto &d = packet["data"];
    if (d.contains("ice_id")) {
        auto idString = [](const Json &id) { return id.is_string() ? id.get<std::string>() : id.dump(); };
        if (idString(d["ice_id"]) != idString(ice_))
            return;
    }
    auto type = d.value("type", std::string{});
    if (type == "answer" || type == "offer" || type == "restart_ice") {
        peer_->setRemoteDescription(
            rtc::Description(d.at("sdp").get<std::string>(), type == "answer" ? "answer" : "offer"));
        if (type != "answer")
            peer_->setLocalDescription(rtc::Description::Type::Answer);
    } else if (type == "candidate") {
        auto c = d.contains("candidate") && d["candidate"].is_object() ? d["candidate"] : d;
        peer_->addRemoteCandidate(rtc::Candidate(
            c.at("candidate").get<std::string>(),
            c.contains("sdpMid") && c["sdpMid"].is_string() ? c["sdpMid"].get<std::string>() : "0"));
    }
}
void Client::input(const Json &data) {
    auto it = channels_.find("CONTROL_DATA_CHANNEL");
    if (it == channels_.end() || !it->second->isOpen())
        return;
    const auto action = data.value("action", std::string{});
    if (it->second->bufferedAmount() > 64 * 1024 &&
        (action == "mouse_move_absolute" || action == "mouse_scroll"))
        return;
    auto s = data.dump();
    try {
        it->second->send(reinterpret_cast<const std::byte *>(s.data()), s.size());
    } catch (...) {
        return;
    }
    if (action == "kbd_press")
        heldKeys_.insert(data.at("key").get<int>());
    else if (action == "kbd_release")
        heldKeys_.erase(data.at("key").get<int>());
    else if (action == "mouse_press")
        heldButtons_.insert(data.at("button").get<int>());
    else if (action == "mouse_release")
        heldButtons_.erase(data.at("button").get<int>());
}
void Client::release() {
    auto keys = heldKeys_, buttons = heldButtons_;
    for (int k : keys)
        input({{"action", "kbd_release"}, {"key", k}});
    for (int b : buttons)
        input({{"action", "mouse_release"}, {"button", b}});
    heldKeys_.clear();
    heldButtons_.clear();
}
void Client::disconnect() {
    diagnostic("disconnect");
    release();
    ++generation_;
    deadline_ = 0;
    qr_ = nullptr;
    earlySoac_.clear();
    acks_.clear();
    remainingAttachments_ = 0;
    nextAck_ = 1;
    for (auto &[name, ch] : channels_)
        ch->close();
    channels_.clear();
    if (peer_) {
        peer_->resetCallbacks();
        peer_->close();
        peer_.reset();
    }
    video_.reset();
    audio_.reset();
    socket_.reset();
    media_->reset();
    {
        std::lock_guard lock(stateMutex_);
        state_.qrUrl.clear();
        state_.streaming = false;
    }
}
} // namespace uu
