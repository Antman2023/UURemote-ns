#pragma once
#include "protocol.hpp"
#include <deque>
#include <rtc/rtc.hpp>
#include <stdexcept>

namespace uu {
enum class RemoteSignal { Ignored, Closed, Description, CandidateQueued, CandidateApplied };
inline bool remoteSignalingClosed(const rtc::PeerConnection &peer) {
    const auto state = peer.state();
    return state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Disconnected ||
           state == rtc::PeerConnection::State::Closed ||
           peer.iceState() == rtc::PeerConnection::IceState::Closed;
}
// Called serially by the client worker. Candidates can precede the remote SDP.
inline RemoteSignal applyRemoteSignal(rtc::PeerConnection &peer, const Json &data,
                                      std::deque<rtc::Candidate> &pending) {
    // Transport teardown is asynchronous. Keep its original state failure instead
    // of replacing it with an exception from a candidate arriving during teardown.
    if (remoteSignalingClosed(peer)) {
        pending.clear();
        return RemoteSignal::Closed;
    }
    try {
        const auto type = data.value("type", std::string{});
        if (type == "answer" || type == "offer" || type == "restart_ice") {
            peer.setRemoteDescription(
                rtc::Description(data.at("sdp").get<std::string>(), type == "answer" ? "answer" : "offer"));
            if (type != "answer")
                peer.setLocalDescription(rtc::Description::Type::Answer);
            while (!pending.empty()) {
                peer.addRemoteCandidate(pending.front());
                pending.pop_front();
            }
            return RemoteSignal::Description;
        }
        if (type != "candidate")
            return RemoteSignal::Ignored;
        const auto &source =
            data.contains("candidate") && data["candidate"].is_object() ? data["candidate"] : data;
        const auto text = source.at("candidate").get<std::string>();
        if (text.empty())
            return RemoteSignal::Ignored; // end of candidates
        rtc::Candidate candidate(text, source.contains("sdpMid") && source["sdpMid"].is_string()
                                           ? source["sdpMid"].get<std::string>()
                                           : "0");
        if (!peer.remoteDescription()) {
            if (pending.size() >= 128)
                throw std::runtime_error("远端 ICE 候选过多");
            pending.push_back(std::move(candidate));
            return RemoteSignal::CandidateQueued;
        }
        peer.addRemoteCandidate(std::move(candidate));
        return RemoteSignal::CandidateApplied;
    } catch (const std::logic_error &) {
        // Teardown may begin between the state check and addRemoteCandidate().
        if (!remoteSignalingClosed(peer))
            throw;
        pending.clear();
        return RemoteSignal::Closed;
    }
}
} // namespace uu
