#include "background.hpp"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <rtc/rtc.hpp>
#include <thread>
#ifdef UU_TEST_NO_DETACH
#include <atomic>
#include <cerrno>
#include <pthread.h>
std::atomic<int> detachCalls{0};
extern "C" int __wrap_pthread_detach(pthread_t) {
    ++detachCalls;
    return ENOSYS;
}
#endif

int main() {
#ifdef UU_TEST_NO_DETACH
    // Prove the runtime models Horizon before exercising the library paths.
    std::thread probe([] {});
    bool unsupported = false;
    try {
        probe.detach();
    } catch (const std::system_error &error) {
        unsupported = error.code().value() == ENOSYS;
    }
    if (probe.joinable())
        probe.join();
    if (!unsupported)
        return 1;
    detachCalls = 0;
#endif
    std::mutex mutex;
    std::condition_variable cv;
    bool input = false, picture = false;
    int result = 1;
    {
        rtc::Configuration config;
        config.disableAutoNegotiation = true;
        auto controller = std::make_shared<rtc::PeerConnection>(config);
        auto host = std::make_shared<rtc::PeerConnection>(config);
        std::weak_ptr<rtc::PeerConnection> weakController = controller, weakHost = host;
        controller->onLocalDescription([weakHost](rtc::Description sdp) {
            if (auto pc = weakHost.lock()) {
                pc->setRemoteDescription(sdp);
                // Non-numeric host must take the resolver path that used detach on Horizon.
                pc->addRemoteCandidate(
                    rtc::Candidate("candidate:resolver-test 1 UDP 2122260223 localhost 9 typ host", "0"));
                pc->setLocalDescription(rtc::Description::Type::Answer);
            }
        });
        host->onLocalDescription([weakController](rtc::Description sdp) {
            if (auto pc = weakController.lock())
                pc->setRemoteDescription(sdp);
        });
        controller->onLocalCandidate([weakHost](rtc::Candidate candidate) {
            if (auto pc = weakHost.lock())
                pc->addRemoteCandidate(candidate);
        });
        host->onLocalCandidate([weakController](rtc::Candidate candidate) {
            if (auto pc = weakController.lock())
                pc->addRemoteCandidate(candidate);
        });
        rtc::Description::Video recv("0", rtc::Description::Direction::RecvOnly);
        recv.addH264Codec(96);
        auto receiver = controller->addTrack(recv);
        auto depacketizer =
            std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::StartSequence);
        depacketizer->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
        receiver->setMediaHandler(depacketizer);
        receiver->onFrame([&](rtc::binary data, rtc::FrameInfo) {
            std::lock_guard lock(mutex);
            picture = data == rtc::binary{std::byte{0},    std::byte{0},    std::byte{0},   std::byte{1},
                                          std::byte{0x65}, std::byte{0x42}, std::byte{0x17}};
            cv.notify_all();
        });
        rtc::Description::Video send("0", rtc::Description::Direction::SendOnly);
        send.addH264Codec(96);
        send.addSSRC(1234, "test");
        auto sender = host->addTrack(send);
        std::vector<std::shared_ptr<rtc::DataChannel>> remoteChannels;
        host->onDataChannel([&](std::shared_ptr<rtc::DataChannel> channel) {
            channel->onMessage(
                [&](rtc::binary data) {
                    std::string value(reinterpret_cast<const char *>(data.data()), data.size());
                    std::lock_guard lock(mutex);
                    input = value == "{\"action\":\"kbd_press\",\"key\":87}";
                    cv.notify_all();
                },
                nullptr);
            std::lock_guard lock(mutex);
            remoteChannels.push_back(std::move(channel));
        });
        auto control = controller->createDataChannel("CONTROL_DATA_CHANNEL");
        controller->setLocalDescription();
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < until && (!control->isOpen() || !sender->isOpen()))
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (control->isOpen() && sender->isOpen()) {
            const std::string message = "{\"action\":\"kbd_press\",\"key\":87}";
            control->send(reinterpret_cast<const std::byte *>(message.data()), message.size());
            const uint8_t packet[] = {0x80, 0xe0, 0, 1, 0, 0, 0, 1, 0, 0, 4, 0xd2, 0x65, 0x42, 0x17};
            sender->send(reinterpret_cast<const std::byte *>(packet), sizeof(packet));
            std::unique_lock lock(mutex);
            cv.wait_until(lock, until, [&] { return input && picture; });
            if (input && picture)
                result = 0;
        }
        controller->resetCallbacks();
        host->resetCallbacks();
        receiver->resetCallbacks();
        controller->close();
        host->close();
    }
    rtc::Cleanup().wait();
    uu::waitBackgroundTasks();
#ifdef UU_TEST_NO_DETACH
    if (detachCalls != 0)
        result = 1;
#endif
    std::cout << (result
                      ? "WebRTC loopback failed"
                      : "WebRTC loopback passed: ICE, DTLS, SCTP binary input and SRTP H.264 depacketization")
              << '\n';
    return result;
}
