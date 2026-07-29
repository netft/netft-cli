#include "platform/clock.hpp"

#include "platform/interrupt.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>

namespace netft_cli {
namespace {

using namespace std::chrono_literals;

TEST(SystemClock, LongWaitReturnsPromptlyWhenInterrupted) {
  SystemClock clock;
  InterruptFlag interrupt;
  const auto started = std::chrono::steady_clock::now();
  auto wait =
      std::async(std::launch::async, [&] { return clock.wait_until(started + 30s, interrupt); });

  std::this_thread::sleep_for(20ms);
  interrupt.request();

  ASSERT_EQ(wait.wait_for(1s), std::future_status::ready);
  EXPECT_FALSE(wait.get());
  EXPECT_LT(std::chrono::steady_clock::now() - started, 1s);
}

} // namespace
} // namespace netft_cli
