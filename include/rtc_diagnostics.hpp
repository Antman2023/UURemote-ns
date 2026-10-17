#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace uu {
// Returns fixed labels and numeric codes only; unrecognized text is discarded.
std::string rtcLogEvent(std::string_view message);
std::string_view sctpHandshakeEvent(bool outgoing, const uint8_t *data, size_t size) noexcept;
void diagnosticSctpHandshake(bool outgoing, const uint8_t *data, size_t size) noexcept;
} // namespace uu
