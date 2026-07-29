#pragma once

#include "platform/clock.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace netft_cli::test {

class FakeClock final : public Clock {
public:
  using SleepHook = std::function<void(std::size_t)>;

  [[nodiscard]] TimePoint now() const override { return now_; }

  void sleep_until(TimePoint deadline) override {
    deadlines_.push_back(deadline);
    now_ = std::max(now_, deadline);
    now_ += next_overshoot_;
    next_overshoot_ = Duration{};
    if (sleep_hook_) {
      sleep_hook_(deadlines_.size());
    }
  }

  void set_next_sleep_overshoot(Duration overshoot) noexcept { next_overshoot_ = overshoot; }
  void set_sleep_hook(SleepHook hook) { sleep_hook_ = std::move(hook); }

  [[nodiscard]] const std::vector<TimePoint> &deadlines() const noexcept { return deadlines_; }

private:
  TimePoint now_{};
  Duration next_overshoot_{};
  SleepHook sleep_hook_;
  std::vector<TimePoint> deadlines_;
};

} // namespace netft_cli::test
