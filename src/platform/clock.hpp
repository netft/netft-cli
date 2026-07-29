#pragma once

#include <chrono>

namespace netft_cli {

class Clock {
public:
  using TimePoint = std::chrono::steady_clock::time_point;
  using Duration = std::chrono::steady_clock::duration;

  virtual ~Clock() = default;
  [[nodiscard]] virtual TimePoint now() const = 0;
  virtual void sleep_until(TimePoint deadline) = 0;
};

class SystemClock final : public Clock {
public:
  [[nodiscard]] TimePoint now() const override;
  void sleep_until(TimePoint deadline) override;
};

} // namespace netft_cli
