#pragma once
#include "protocol.hpp"

namespace uu {
// Keep commands and RTC callbacks responsive even under sustained signaling.
// now() returns monotonic milliseconds; one I/O call itself must be nonblocking.
template <typename Receive, typename Dispatch, typename Interrupted, typename Now>
void pumpSignaling(Receive receive, Dispatch dispatch, Interrupted interrupted, Now now) {
    const auto end = now() + 8;
    for (int count = 0; count < 16 && !interrupted() && now() < end; ++count) {
        WsMessage message;
        if (!receive(message))
            break;
        dispatch(message);
    }
}
} // namespace uu
