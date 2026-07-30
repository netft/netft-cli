#include "commands/check.hpp"

#include "app/error.hpp"
#include "support/assertions.hpp"
#include "support/fake_backend.hpp"
#include "support/fake_clock.hpp"
#include "support/memory_output.hpp"
#include "support/options.hpp"
#include "support/records.hpp"
#include "support/structured_parsers.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace netft_cli {
namespace {

using namespace std::chrono_literals;

netft::Sample accepted_sample(std::uint32_t sequence = 1) {
  auto sample = test::sample(sequence);
  sample.status = 0;
  return sample;
}

test::FakeBackend healthy_backend() {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_samples({accepted_sample()});
  auto health = test::health();
  health.received_count = 0;
  backend.session().set_health(health);
  return backend;
}

TEST(CheckCommand, EmitsJsonForSuccessfulBoundedCheck) {
  auto backend = healthy_backend();
  test::FakeClock clock;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  EXPECT_EQ(run_check(test::check_options(), backend, output.context(), interrupt, clock), 0);

  const auto document = test::parse_json(output.standard_output_text());
  EXPECT_EQ(document.at("schema_version"), 1);
  EXPECT_EQ(document.at("result"), "pass");
  EXPECT_TRUE(document.at("checks").is_array());
  EXPECT_EQ(document.at("metrics").at("sample_count"), 1);
  EXPECT_TRUE(output.standard_error_text().empty());
  ASSERT_EQ(clock.deadlines().size(), 1U);
  EXPECT_EQ(clock.deadlines().front().time_since_epoch(), 100ms);
}

TEST(CheckCommand, ReturnsAcceptanceFailureForMissingSampleAndThresholds) {
  test::FakeBackend empty_backend;
  empty_backend.set_configuration(test::configuration());
  empty_backend.session().set_health(test::health());
  test::FakeClock empty_clock;
  test::MemoryOutput empty_output(false);
  InterruptFlag empty_interrupt;

  EXPECT_EQ(run_check(test::check_options(), empty_backend, empty_output.context(), empty_interrupt,
                      empty_clock),
            static_cast<int>(ExitCode::Acceptance));
  EXPECT_EQ(test::parse_json(empty_output.standard_output_text()).at("result"), "fail");

  auto threshold_backend = healthy_backend();
  auto options = test::check_options();
  options.min_rate_hz = 100.0;
  options.max_loss_percent = 0.0;
  options.max_reconnects = 0;
  auto health = test::health();
  health.received_count = 99;
  health.lost_count = 1;
  health.reconnect_count = 1;
  test::FakeClock threshold_clock;
  threshold_clock.set_sleep_hook(
      [&](std::size_t) { threshold_backend.session().set_health(health); });
  test::MemoryOutput threshold_output(false);
  InterruptFlag threshold_interrupt;

  EXPECT_EQ(run_check(options, threshold_backend, threshold_output.context(), threshold_interrupt,
                      threshold_clock),
            static_cast<int>(ExitCode::Acceptance));
}

TEST(CheckCommand, ReturnsSuccessWithWarningForUnthresholdedLoss) {
  auto backend = healthy_backend();
  auto health = test::health();
  health.received_count = 99;
  health.lost_count = 1;
  test::FakeClock clock;
  clock.set_sleep_hook([&](std::size_t) { backend.session().set_health(health); });
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  EXPECT_EQ(run_check(test::check_options(), backend, output.context(), interrupt, clock), 0);
  EXPECT_EQ(test::parse_json(output.standard_output_text()).at("result"), "pass_with_warnings");
}

TEST(CheckCommand, MapsSensorAndTransportFaultsToOperationalErrors) {
  for (const auto &[fault, expected] :
       {std::pair{netft::FaultCode::SeriousStatus, ExitCode::Sensor},
        std::pair{netft::FaultCode::Socket, ExitCode::Stream}}) {
    auto backend = healthy_backend();
    auto health = test::health();
    health.state = netft::ClientState::Faulted;
    health.fault_code = fault;
    backend.session().set_health(health);
    test::FakeClock clock;
    test::MemoryOutput output(false);
    InterruptFlag interrupt;

    test::expect_app_error(expected, [&] {
      static_cast<void>(
          run_check(test::check_options(), backend, output.context(), interrupt, clock));
    });
    EXPECT_TRUE(output.standard_output_text().empty());
  }
}

TEST(CheckCommand, InterruptionStopsWithoutEmittingPartialResult) {
  auto backend = healthy_backend();
  test::FakeClock clock;
  clock.set_sleep_hook([](std::size_t) {});
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  interrupt.request();

  EXPECT_EQ(run_check(test::check_options(), backend, output.context(), interrupt, clock),
            static_cast<int>(ExitCode::Interrupted));
  EXPECT_TRUE(output.standard_output_text().empty());
  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(CheckCommand, TextAndJsonRenderTheSameOutcome) {
  auto json_backend = healthy_backend();
  test::FakeClock json_clock;
  test::MemoryOutput json_output(false);
  InterruptFlag json_interrupt;
  EXPECT_EQ(run_check(test::check_options(), json_backend, json_output.context(), json_interrupt,
                      json_clock),
            0);

  auto text_backend = healthy_backend();
  auto text_options = test::check_options();
  text_options.format = OutputFormat::Text;
  test::FakeClock text_clock;
  test::MemoryOutput text_output(false);
  InterruptFlag text_interrupt;
  EXPECT_EQ(
      run_check(text_options, text_backend, text_output.context(), text_interrupt, text_clock), 0);

  EXPECT_EQ(test::parse_json(json_output.standard_output_text()).at("result"), "pass");
  EXPECT_FALSE(text_output.standard_output_text().empty());
}

} // namespace
} // namespace netft_cli
