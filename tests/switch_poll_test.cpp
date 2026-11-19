#include "switch_compat/poll.hpp"
#include <iostream>
#include <stdexcept>

namespace {
struct Socket {
    int fd;
    short events, revents;
};

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        auto unexpectedWait = [](int) { throw std::runtime_error("Waited with active sockets"); };
        Socket slots[]{{-1, 0, 7}, {2, 1, 7}, {-2, 0, 7}, {4, 4, 7}};
        auto result = uu::pollActiveSockets(slots, 4, 80, [](Socket *active, std::size_t count, int timeout) {
            check(count == 2 && timeout == 80, "Incorrect active descriptor count or timeout");
            check(active[0].fd == 2 && active[1].fd == 4 && !active[0].revents && !active[1].revents,
                  "Backend received ignored slots or stale output events");
            active[1].revents = 4;
            return 1;
        }, unexpectedWait);
        check(result == 1 && slots[0].fd == -1 && slots[2].fd == -2 && slots[3].events == 4 &&
                  !slots[0].revents && !slots[1].revents && !slots[2].revents && slots[3].revents == 4,
              "Poll events were not returned to their original slots");

        Socket ignored{-1, 0, 7};
        int waited = -1;
        auto unexpectedPoll = [](Socket *, std::size_t, int) -> int { throw std::runtime_error("Polled an empty set"); };
        check(uu::pollActiveSockets(&ignored, 1, 30, unexpectedPoll, [&](int ms) { waited = ms; }) == 0 &&
                  waited == 30 && !ignored.revents,
              "An entirely ignored set did not honor its timeout");

        Socket single{1, 1, 5};
        result = uu::pollActiveSockets(&single, 1, 0, [&](Socket *active, std::size_t count, int timeout) {
            check(active == &single && count == 1 && timeout == 0 && !active->revents,
                  "A valid poll set was not passed through with fresh output events");
            errno = EINTR;
            return -1;
        }, unexpectedWait);
        check(result == -1 && errno == EINTR, "The polling error was lost");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
