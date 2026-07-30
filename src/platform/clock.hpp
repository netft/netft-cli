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

class WallClock {
public:
  using TimePoint = std::chrono::system_clock::time_point;

  virtual ~WallClock() = default;
  [[nodiscard]] virtual TimePoint now() const = 0;
};

class SystemWallClock final : public WallClock {
public:
  [[nodiscard]] TimePoint now() const override;
};

} // namespace netft_cli
