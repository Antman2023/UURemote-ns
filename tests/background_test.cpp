#include "background.hpp"
#include "diagnostics.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <system_error>

int main() {
    try {
        std::promise<void> release;
        auto gate = release.get_future().share();
        std::atomic<int> finished{0};
        for (int i = 0; i < 16; ++i)
            uu::backgroundTask([&, gate] {
                gate.wait();
                ++finished;
            });
        // Discarding returned futures must leave ownership with the registry.
        release.set_value();
        uu::backgroundTask([&] { uu::backgroundTask([&] { ++finished; }); });
        std::promise<int> moved;
        auto value = moved.get_future();
        uu::backgroundTask([](std::promise<int> promise) { promise.set_value(42); }, std::move(moved));
        auto failed = uu::backgroundTask([] { throw std::runtime_error("private-test-payload"); });
        uu::waitBackgroundTasks();
        if (finished != 17 || value.get() != 42)
            throw std::runtime_error("Background jobs did not finish");
        std::exception_ptr error;
        try {
            failed.get();
        } catch (...) {
            error = std::current_exception();
        }
        if (uu::exceptionSummary(error) != "exception-runtime-error")
            throw std::runtime_error("Background exception must be retained without its payload");
        try {
            throw std::system_error(88, std::generic_category(), "private-test-payload");
        } catch (...) {
            error = std::current_exception();
        }
        if (uu::exceptionSummary(error) != "exception-system-error-88")
            throw std::runtime_error("Numeric system error diagnostic");
        uu::waitBackgroundTasks();
        std::cout
            << "Background ownership, nested jobs, draining and private exception diagnostics passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
