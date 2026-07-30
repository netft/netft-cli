#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace netft_cli {

enum class ExitCode {
  Usage = 2,
  Discovery = 3,
  Stream = 4,
  Sensor = 5,
  Io = 6,
  Acceptance = 7,
  Recording = 8,
  Interrupted = 130,
};

class AppError : public std::runtime_error {
public:
  AppError(ExitCode code, std::string_view message)
      : std::runtime_error(std::string{message}), code_(code) {}

  [[nodiscard]] ExitCode code() const noexcept { return code_; }

private:
  ExitCode code_;
};

} // namespace netft_cli
