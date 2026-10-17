#pragma once
#include <exception>
#include <string>
namespace uu {
void diagnostic(const char *event) noexcept;
std::string exceptionSummary(std::exception_ptr error);
void diagnosticException(const char *event, std::exception_ptr error) noexcept;
[[noreturn]] void terminateWithDiagnostics() noexcept;
const char *diagnosticPath() noexcept;
std::string connectionFailure(const std::string &stage, const std::string &error);
} // namespace uu
