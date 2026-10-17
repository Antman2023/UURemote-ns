#pragma once
#include "protocol.hpp"
#include <SDL.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace uu {
struct Picture {
    int width = 0, height = 0;
    uint64_t serial = 0;
    Bytes pixels;
};
class Media {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    Media();
    ~Media();
    void video(const uint8_t *data, size_t size, uint64_t generation);
    void audio(const uint8_t *data, size_t size, uint64_t generation);
    uint64_t generation() const;
    Picture picture(uint64_t after);
    void reset();
    std::string status();
};
} // namespace uu
