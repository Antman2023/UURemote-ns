#include "background.hpp"
#include "diagnostics.hpp"
#include <algorithm>
#include <mutex>
#include <vector>

namespace uu {
namespace {
struct Tasks {
    std::mutex mutex;
    std::vector<std::shared_future<void>> pending;
};
Tasks &tasks() {
    // Explicitly drained after rtc::Cleanup, before closing platform services.
    static auto *instance = new Tasks;
    return *instance;
}
} // namespace
std::shared_future<void> startBackgroundTask(std::function<void()> task) {
    auto result = std::async(std::launch::async, [task = std::move(task)] {
                      try {
                          task();
                      } catch (...) {
                          diagnosticException("background-exception", std::current_exception());
                          throw;
                      }
                  }).share();
    auto &registry = tasks();
    std::lock_guard lock(registry.mutex);
    auto &pending = registry.pending;
    pending.erase(std::remove_if(pending.begin(), pending.end(),
                                 [](const auto &future) {
                                     return future.wait_for(std::chrono::seconds(0)) ==
                                            std::future_status::ready;
                                 }),
                  pending.end());
    pending.push_back(result);
    return result;
}
void waitBackgroundTasks() {
    auto &registry = tasks();
    for (;;) {
        std::vector<std::shared_future<void>> pending;
        {
            std::lock_guard lock(registry.mutex);
            pending.swap(registry.pending);
        }
        if (pending.empty())
            return;
        // Never hold the registry lock while joining; jobs may create other jobs.
        for (auto &future : pending)
            future.wait();
    }
}
} // namespace uu
