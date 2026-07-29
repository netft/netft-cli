#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace netft_cli {

enum class ExitCode {
  Usage = 2,
  Discovery = 3,
  Stream = 4,
  Sensor = 5,
  Io = 6,
  Interrupted = 130,
};

class AppError : public std::runtime_error {
public:
  AppError(ExitCode code, std::string message)
      : std::runtime_error(std::move(message)), code_(code) {}

  ExitCode code() const noexcept { return code_; }

private:
  ExitCode code_;
};

} // namespace netft_cli
