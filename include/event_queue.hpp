#pragma once
#include <cstddef>
#include <mutex>
#include <utility>

namespace uu {
// Pop only the event being handled. If its handler throws, later commands stay
// queued. Only this consumer removes entries; producers may append concurrently.
// Events posted by handlers belong to the next batch, avoiding starvation.
template <typename Queue, typename Stop, typename Handle>
void drainEventBatch(Queue &queue, std::mutex &mutex, Stop stop, Handle handle) {
    std::size_t remaining;
    {
        std::lock_guard lock(mutex);
        remaining = queue.size();
    }
    while (remaining > 0 && !stop()) {
        --remaining;
        auto event = [&] {
            std::lock_guard lock(mutex);
            auto value = std::move(queue.front());
            queue.pop_front();
            return value;
        }();
        handle(event);
    }
}
} // namespace uu
