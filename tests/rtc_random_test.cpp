#include "utils.hpp"
#include <array>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

std::atomic<int> destroyed{0};
struct TrackedGenerator : std::mt19937 {
    explicit TrackedGenerator(std::seed_seq &seed) : std::mt19937(seed) {}
    ~TrackedGenerator() {
        ++destroyed;
    }
};

// Exercise the Horizon pthread-key branch, including simultaneous first use,
// per-thread isolation, retained state and both generator instantiations.
int main() {
    try {
        constexpr int count = 8;
        std::array<const void *, count> addresses{};
        std::array<std::thread, count> threads;
        std::atomic<int> ready{0};
        std::atomic<bool> passed{true};
        for (int i = 0; i < count; ++i)
            threads[i] = std::thread([&, i] {
                try {
                    auto random = rtc::impl::utils::random_engine();
                    rtc::impl::utils::random_engine<TrackedGenerator>()();
                    addresses[i] = &random.engine;
                    auto expected = random.engine;
                    if (random() != expected() || rtc::impl::utils::random_engine()() != expected())
                        passed = false;
                    auto bytes = rtc::impl::utils::random_bytes_engine();
                    for (int n = 0; n < 256; ++n)
                        if (bytes() > 255)
                            passed = false;
                } catch (...) {
                    passed = false;
                }
                ++ready;
                while (ready != count)
                    std::this_thread::yield();
            });
        for (auto &thread : threads)
            thread.join();
        if (destroyed != count)
            passed = false;
        for (int i = 0; i < count; ++i)
            for (int j = 0; j < i; ++j)
                if (addresses[i] == addresses[j])
                    passed = false;
        if (!passed)
            throw std::runtime_error("Random engine thread state regression");
        std::cout << "Pthread random engines: concurrent initialization, isolation and state passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
