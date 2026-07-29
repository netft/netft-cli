#include "platform/clock.hpp"

#include "platform/interrupt.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace netft_cli {

Clock::TimePoint SystemClock::now() const { return std::chrono::steady_clock::now(); }

bool SystemClock::wait_until(TimePoint deadline, const InterruptFlag &interrupt) {
  using namespace std::chrono_literals;
  constexpr auto interrupt_poll_interval = 50ms;

  while (!interrupt.requested()) {
    const auto current = std::chrono::steady_clock::now();
    if (current >= deadline) {
      return true;
    }
    std::this_thread::sleep_until(std::min(deadline, current + interrupt_poll_interval));
  }
  return false;
}

} // namespace netft_cli
