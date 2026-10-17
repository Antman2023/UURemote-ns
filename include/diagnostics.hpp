#pragma once
#include <string>
namespace uu {
void diagnostic(const char *event) noexcept;
const char *diagnosticPath() noexcept;
std::string connectionFailure(const std::string &stage, const std::string &error);
} // namespace uu
