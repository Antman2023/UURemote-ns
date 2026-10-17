#pragma once
#include "protocol.hpp"
#include <deque>
#include <rtc/rtc.hpp>
#include <stdexcept>

namespace uu {
enum class RemoteSignal { Ignored, Description, CandidateQueued, CandidateApplied };
// Called serially by the client worker. Candidates can precede the remote SDP.
inline RemoteSignal applyRemoteSignal(rtc::PeerConnection &peer, const Json &data,
                                      std::deque<rtc::Candidate> &pending) {
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
}
} // namespace uu
