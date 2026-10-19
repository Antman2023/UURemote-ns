#include "event_queue.hpp"
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>

void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Event queue regression");
}
int main() {
    try {
        std::mutex mutex;
        // A failed RTC event must not swallow a queued logout and cancellation.
        std::deque<int> queue{1, 2, 3};
        bool failed = false;
        try {
            uu::drainEventBatch(
                queue, mutex, [] { return false; }, [](int) { throw std::runtime_error("RTC failed"); });
        } catch (const std::runtime_error &) {
            failed = true;
        }
        check(failed && queue == std::deque<int>({2, 3}));
        std::vector<int> processed;
        uu::drainEventBatch(
            queue, mutex, [] { return false; },
            [&](int event) {
                // Handlers can post without deadlocking; new events wait for the next batch.
                std::lock_guard lock(mutex);
                processed.push_back(event);
                queue.push_back(event + 10);
            });
        check(processed == std::vector<int>({2, 3}) && queue == std::deque<int>({12, 13}));
        bool stop = false;
        uu::drainEventBatch(
            queue, mutex, [&] { return stop; },
            [&](int event) {
                processed.push_back(event);
                stop = true;
            });
        check(queue == std::deque<int>({13}) && processed.back() == 12);
        uu::drainEventBatch(
            queue, mutex, [] { return false; }, [&](int event) { processed.push_back(event); });
        check(queue.empty() && processed == std::vector<int>({2, 3, 12, 13}));
        std::cout << "Queued commands survive errors and handlers can post without starvation.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
