#include "stream/health_collector.hpp"

#include "support/records.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace netft_cli {
namespace {

using namespace std::chrono_literals;

TEST(HealthCollector, CollectsWindowRatesCounterDeltasAndSensorStatus) {
  auto baseline = test::health();
  baseline.received_count = 100;
  baseline.lost_count = 2;
  baseline.duplicate_count = 3;
  baseline.out_of_order_count = 4;
  baseline.reconnect_count = 5;
  HealthCollector collector(Clock::TimePoint{1s}, baseline);

  collector.observe(test::sample(101));
  auto warning_sample = test::sample(102);
  warning_sample.status = 0x10;
  collector.observe(warning_sample);

  auto final = baseline;
  final.received_count = 1'100;
  final.lost_count = 7;
  final.duplicate_count = 5;
  final.out_of_order_count = 5;
  final.reconnect_count = 7;
  const auto observation = collector.finish(Clock::TimePoint{3s}, final);

  EXPECT_DOUBLE_EQ(observation.elapsed_seconds, 2.0);
  EXPECT_EQ(observation.sample_count, 2U);
  EXPECT_DOUBLE_EQ(observation.observed_rate_hz, 1.0);
  EXPECT_EQ(observation.received_count, 1'000U);
  EXPECT_EQ(observation.lost_count, 5U);
  EXPECT_EQ(observation.duplicate_count, 2U);
  EXPECT_EQ(observation.out_of_order_count, 1U);
  EXPECT_EQ(observation.reconnect_count, 2U);
  EXPECT_EQ(observation.nonzero_status_count, 2U);
  EXPECT_EQ(observation.last_status, 0x10U);
}

TEST(HealthCollector, RepresentsNoDataWithoutDividingByZero) {
  const auto baseline = test::health();
  HealthCollector collector(Clock::TimePoint{4s}, baseline);

  const auto observation = collector.finish(Clock::TimePoint{4s}, baseline);

  EXPECT_EQ(observation.sample_count, 0U);
  EXPECT_DOUBLE_EQ(observation.elapsed_seconds, 0.0);
  EXPECT_DOUBLE_EQ(observation.observed_rate_hz, 0.0);
  EXPECT_EQ(observation.nonzero_status_count, 0U);
}

} // namespace
} // namespace netft_cli
