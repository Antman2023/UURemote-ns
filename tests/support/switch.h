#pragma once
#include <cstdint>
using Result = std::uint32_t;
using u32 = std::uint32_t;
enum SslIoMode { SslIoMode_Blocking = 1, SslIoMode_NonBlocking = 2 };
struct SslConnection {
    SslIoMode mode = SslIoMode_Blocking;
};
#define R_FAILED(result) ((result) != 0)
extern "C" Result sslConnectionSetIoMode(SslConnection *, SslIoMode);
