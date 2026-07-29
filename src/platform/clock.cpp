#include "platform/clock.hpp"

#include <thread>

namespace netft_cli {

Clock::TimePoint SystemClock::now() const { return std::chrono::steady_clock::now(); }

void SystemClock::sleep_until(TimePoint deadline) { std::this_thread::sleep_until(deadline); }

} // namespace netft_cli
