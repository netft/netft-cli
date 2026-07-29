#include "monitor/latest_sample.hpp"

#include "platform/interrupt.hpp"
#include "support/records.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>

namespace netft_cli {
namespace {

using namespace std::chrono_literals;

TEST(LatestSampleSlot, ReplacesUnreadSampleWithoutQueueing) {
  LatestSampleSlot slot;
  slot.publish(test::sample(10));
  slot.publish(test::sample(11));

  const auto latest = slot.snapshot();

  ASSERT_TRUE(latest);
  EXPECT_EQ(latest->rdt_sequence, 11U);
}

TEST(LatestSampleSlot, WaitForFirstReturnsWhenASampleIsPublished) {
  LatestSampleSlot slot;
  InterruptFlag interrupt;
  auto waiter = std::async(std::launch::async, [&] { return slot.wait_for_first(1s, interrupt); });

  slot.publish(test::sample(12));

  EXPECT_TRUE(waiter.get());
  const auto latest = slot.snapshot();
  ASSERT_TRUE(latest);
  EXPECT_EQ(latest->rdt_sequence, 12U);
}

TEST(LatestSampleSlot, WaitForFirstStopsOnInterruption) {
  LatestSampleSlot slot;
  InterruptFlag interrupt;
  interrupt.request();

  EXPECT_FALSE(slot.wait_for_first(100ms, interrupt));
}

} // namespace
} // namespace netft_cli
