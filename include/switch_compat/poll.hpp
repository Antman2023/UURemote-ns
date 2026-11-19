#pragma once
#include <array>
#include <cerrno>
#include <cstddef>
#include <memory>
#include <new>

namespace uu {
template <typename Descriptor, typename Poll, typename Wait>
int pollActiveSockets(Descriptor *sockets, std::size_t count, int timeout, Poll poll, Wait wait) {
    if (!sockets && count) {
        errno = EFAULT;
        return -1;
    }

    std::size_t active = 0;
    for (std::size_t i = 0; i < count; ++i) {
        sockets[i].revents = 0;
        if (sockets[i].fd >= 0)
            ++active;
    }
    if (!active) {
        wait(timeout);
        return 0;
    }
    if (active == count)
        return poll(sockets, count, timeout);

    std::array<Descriptor, 16> local;
    std::unique_ptr<Descriptor[]> allocated;
    auto *entries = local.data();
    if (active > local.size()) {
        allocated.reset(new (std::nothrow) Descriptor[active]);
        if (!allocated) {
            errno = ENOMEM;
            return -1;
        }
        entries = allocated.get();
    }

    std::size_t next = 0;
    for (std::size_t i = 0; i < count; ++i)
        if (sockets[i].fd >= 0)
            entries[next++] = sockets[i];

    const int result = poll(entries, active, timeout);
    if (result > 0) {
        next = 0;
        for (std::size_t i = 0; i < count; ++i)
            if (sockets[i].fd >= 0)
                sockets[i].revents = entries[next++].revents;
    }
    return result;
}
} // namespace uu
