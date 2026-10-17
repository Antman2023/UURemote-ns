#include "signal_pump.hpp"
#include <iostream>
#include <stdexcept>

void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Signaling starvation regression");
}
int main() {
    try {
        int64_t clock = 0;
        int reads = 0, dispatched = 0;
        bool interrupted = false, rtcEventPending = false;
        auto receive = [&](uu::WsMessage &) {
            ++reads;
            return true;
        };
        auto dispatch = [&](const uu::WsMessage &) {
            ++dispatched;
            rtcEventPending = true;
        };
        uu::pumpSignaling(receive, dispatch, [&] { return interrupted; }, [&] { return clock; });
        check(reads == 16 && dispatched == 16 && rtcEventPending);
        reads = dispatched = 0;
        // Model I/O which consumes the budget while connected is queued. The next
        // receive must not run first, even with an endless stream of candidates.
        uu::pumpSignaling(
            [&](uu::WsMessage &) {
                ++reads;
                clock += 1000;
                return true;
            },
            dispatch, [&] { return interrupted; }, [&] { return clock; });
        check(reads == 1 && dispatched == 1);
        reads = dispatched = 0;
        uu::pumpSignaling(
            receive,
            [&](const uu::WsMessage &) {
                ++dispatched;
                interrupted = true;
            },
            [&] { return interrupted; }, [&] { return clock; });
        check(reads == 1 && dispatched == 1);
        reads = 0;
        uu::pumpSignaling(receive, dispatch, [&] { return interrupted; }, [&] { return clock; });
        check(reads == 0); // A queued cancellation must not incur another read.
        interrupted = false;
        uu::pumpSignaling(
            [&](uu::WsMessage &) {
                ++reads;
                return false;
            },
            dispatch, [&] { return interrupted; }, [&] { return clock; });
        check(reads == 1 && dispatched == 1);
        std::cout << "Signaling yields to RTC events and cancellation under packet flood.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
