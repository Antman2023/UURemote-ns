#pragma once
#include <functional>
#include <future>
#include <memory>
#include <tuple>
#include <utility>

namespace uu {
// Retain asynchronous thread ownership until completion. Horizon has join but no detach.
std::shared_future<void> startBackgroundTask(std::function<void()> task);
template <class Function, class... Args>
std::shared_future<void> backgroundTask(Function &&function, Args &&...args) {
    auto call = [function = std::forward<Function>(function),
                 arguments = std::make_tuple(std::forward<Args>(args)...)]() mutable {
        std::apply(std::move(function), std::move(arguments));
    };
    auto owned = std::make_shared<decltype(call)>(std::move(call));
    return startBackgroundTask([owned = std::move(owned)] { (*owned)(); });
}
void waitBackgroundTasks();
} // namespace uu
