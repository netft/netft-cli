#pragma once

#include <chrono>

namespace netft_cli {

class InterruptFlag;

class Clock {
public:
  using TimePoint = std::chrono::steady_clock::time_point;
  using Duration = std::chrono::steady_clock::duration;

  virtual ~Clock() = default;
  [[nodiscard]] virtual TimePoint now() const = 0;
  [[nodiscard]] virtual bool wait_until(TimePoint deadline, const InterruptFlag &interrupt) = 0;
};

class SystemClock final : public Clock {
public:
  [[nodiscard]] TimePoint now() const override;
  [[nodiscard]] bool wait_until(TimePoint deadline, const InterruptFlag &interrupt) override;
};

} // namespace netft_cli
