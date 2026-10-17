#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <switch.h>

extern "C" Result __wrap_sslConnectionRead(SslConnection *, void *, u32, u32 *);
extern "C" Result __wrap_sslConnectionWrite(SslConnection *, const void *, u32, u32 *);
namespace {
constexpr Result wouldBlock = 123 + (204 << 9), modeError = 123 + (10 << 9);
std::string pending, sent;
bool failMode = false;
int calls = 0;
void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Switch TLS I/O regression");
}
} // namespace
extern "C" Result sslConnectionSetIoMode(SslConnection *connection, SslIoMode mode) {
    if (failMode)
        return modeError;
    connection->mode = mode;
    return 0;
}
extern "C" Result __real_sslConnectionRead(SslConnection *connection, void *buffer, u32 size, u32 *read) {
    ++calls;
    // Simulate a service which would wait indefinitely in blocking mode.
    check(connection->mode == SslIoMode_NonBlocking);
    if (pending.empty())
        return wouldBlock;
    *read = std::min(size, u32(pending.size()));
    std::memcpy(buffer, pending.data(), *read);
    pending.erase(0, *read);
    return 0;
}
extern "C" Result __real_sslConnectionWrite(SslConnection *connection, const void *buffer, u32 size,
                                            u32 *written) {
    ++calls;
    check(connection->mode == SslIoMode_NonBlocking);
    if (sent.size() >= 2)
        return wouldBlock;
    *written = std::min(size, u32(2 - sent.size()));
    sent.append(static_cast<const char *>(buffer), *written);
    return 0;
}
int main() {
    try {
        SslConnection connection;
        char buffer[8]{};
        u32 count = 99;
        check(__wrap_sslConnectionRead(&connection, buffer, sizeof(buffer), &count) == wouldBlock);
        check(count == 0); // Would-block must not become a successful EOF.
        pending = "answer";
        check(__wrap_sslConnectionRead(&connection, buffer, 3, &count) == 0);
        check(count == 3 && std::string(buffer, count) == "ans");
        check(__wrap_sslConnectionRead(&connection, buffer, sizeof(buffer), &count) == 0);
        check(count == 3 && std::string(buffer, count) == "wer");
        connection.mode = SslIoMode_Blocking;
        check(__wrap_sslConnectionWrite(&connection, "offer", 5, &count) == 0 && count == 2);
        check(sent == "of");
        check(__wrap_sslConnectionWrite(&connection, "fer", 3, &count) == wouldBlock && count == 0);
        const auto previousCalls = calls;
        failMode = true;
        count = 99;
        check(__wrap_sslConnectionRead(&connection, buffer, sizeof(buffer), &count) == modeError &&
              count == 0);
        count = 99;
        check(__wrap_sslConnectionWrite(&connection, "x", 1, &count) == modeError && count == 0);
        check(calls == previousCalls);
        std::cout << "TLS would-block, partial I/O and mode failures preserved.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
