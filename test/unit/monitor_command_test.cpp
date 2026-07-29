#include "commands/monitor.hpp"

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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace netft_cli {
namespace {

using namespace std::chrono_literals;

std::string read_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

TEST(MonitorCommand, SamplesLatestValueAtTwentyHertz) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_samples({test::sample(1), test::sample(2), test::sample(3)});
  backend.session().set_health(test::health());
  test::FakeClock clock;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  EXPECT_EQ(run_monitor(test::monitor_for(100ms), backend, output.context(), interrupt, clock), 0);

  const auto documents = test::parse_ndjson(output.standard_output_text());
  ASSERT_EQ(documents.size(), 2U);
  EXPECT_EQ(documents.back()["rdt_sequence"], 3U);
  ASSERT_EQ(clock.deadlines().size(), 2U);
  EXPECT_EQ(clock.deadlines()[0].time_since_epoch(), 50ms);
  EXPECT_EQ(clock.deadlines()[1].time_since_epoch(), 100ms);
  EXPECT_EQ(backend.discover_calls(), 1U);
  EXPECT_EQ(backend.open_calls(), 1U);
  EXPECT_EQ(backend.session().start_calls(), 1U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(MonitorCommand, UsesExplicitRateWithoutRelativeSleepDrift) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_samples({test::sample(7)});
  backend.session().set_health(test::health());
  auto options = test::monitor_for(300ms);
  options.rate_hz = 10.0;
  test::FakeClock clock;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  EXPECT_EQ(run_monitor(options, backend, output.context(), interrupt, clock), 0);

  ASSERT_EQ(clock.deadlines().size(), 3U);
  EXPECT_EQ(clock.deadlines()[0].time_since_epoch(), 100ms);
  EXPECT_EQ(clock.deadlines()[1].time_since_epoch(), 200ms);
  EXPECT_EQ(clock.deadlines()[2].time_since_epoch(), 300ms);
  EXPECT_EQ(test::parse_ndjson(output.standard_output_text()).size(), 3U);
}

TEST(MonitorCommand, BoundedDurationCompletesSuccessfullyWithoutPastEndRecord) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_samples({test::sample(8)});
  backend.session().set_health(test::health());
  test::FakeClock clock;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  EXPECT_EQ(run_monitor(test::monitor_for(125ms), backend, output.context(), interrupt, clock), 0);

  EXPECT_EQ(test::parse_ndjson(output.standard_output_text()).size(), 2U);
  ASSERT_EQ(clock.deadlines().size(), 2U);
  EXPECT_LE(clock.deadlines().back().time_since_epoch(), 125ms);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(MonitorCommand, SkipsMissedDeadlinesInsteadOfReplayingHistory) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_samples({test::sample(1), test::sample(2), test::sample(3)});
  backend.session().set_health(test::health());
  test::FakeClock clock;
  clock.set_next_sleep_overshoot(125ms);
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  EXPECT_EQ(run_monitor(test::monitor_for(200ms), backend, output.context(), interrupt, clock), 0);

  const auto documents = test::parse_ndjson(output.standard_output_text());
  ASSERT_EQ(documents.size(), 2U);
  EXPECT_EQ(documents[0]["rdt_sequence"], 3U);
  EXPECT_EQ(documents[1]["rdt_sequence"], 3U);
  ASSERT_EQ(clock.deadlines().size(), 2U);
  EXPECT_EQ(clock.deadlines()[0].time_since_epoch(), 50ms);
  EXPECT_EQ(clock.deadlines()[1].time_since_epoch(), 200ms);
}

TEST(MonitorCommand, WritesCsvOnlyWhenExplicitlySelected) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_samples({test::sample(9)});
  backend.session().set_health(test::health());
  auto options = test::monitor_for(100ms);
  options.format = OutputFormat::Csv;
  test::FakeClock clock;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  EXPECT_EQ(run_monitor(options, backend, output.context(), interrupt, clock), 0);

  const auto table = test::parse_csv(output.standard_output_text());
  EXPECT_EQ(table.header_count(), 1U);
  EXPECT_EQ(table.row_count(), 2U);
  EXPECT_TRUE(table.has_columns({"rdt_sequence", "force_unit", "state"}));
}

TEST(MonitorCommand, AutomaticRedirectedOutputUsesNdjson) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_samples({test::sample(10)});
  backend.session().set_health(test::health());
  auto options = test::monitor_for(50ms);
  options.format = OutputFormat::Automatic;
  test::FakeClock clock;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  EXPECT_EQ(run_monitor(options, backend, output.context(), interrupt, clock), 0);

  const auto documents = test::parse_ndjson(output.standard_output_text());
  ASSERT_EQ(documents.size(), 1U);
  EXPECT_EQ(documents.front()["rdt_sequence"], 10U);
}

TEST(MonitorCommand, AutomaticTerminalOutputUsesHumanMonitor) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_samples({test::sample(11)});
  backend.session().set_health(test::health());
  auto options = test::monitor_for(50ms);
  options.format = OutputFormat::Automatic;
  test::FakeClock clock;
  test::MemoryOutput output(true);
  InterruptFlag interrupt;

  EXPECT_EQ(run_monitor(options, backend, output.context(), interrupt, clock), 0);

  EXPECT_NE(output.standard_output_text().find("rdt=11"), std::string::npos);
  EXPECT_EQ(output.standard_output_text().find('{'), std::string::npos);
}

TEST(MonitorCommand, AutomaticFileOutputUsesNdjsonAndLeavesStdoutEmpty) {
  const auto path = std::filesystem::path(testing::TempDir()) / "netft-monitor-auto.ndjson";
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_samples({test::sample(12)});
  backend.session().set_health(test::health());
  auto options = test::monitor_for(50ms);
  options.format = OutputFormat::Automatic;
  options.output = path;
  test::FakeClock clock;
  test::MemoryOutput output(true);
  InterruptFlag interrupt;

  EXPECT_EQ(run_monitor(options, backend, output.context(), interrupt, clock), 0);

  const auto documents = test::parse_ndjson(read_file(path));
  ASSERT_EQ(documents.size(), 1U);
  EXPECT_EQ(documents.front()["rdt_sequence"], 12U);
  EXPECT_TRUE(output.standard_output_text().empty());
  EXPECT_TRUE(std::filesystem::remove(path));
}

TEST(MonitorCommand, OpensOutputBeforeContactingSensor) {
  test::FakeBackend backend;
  auto options = test::monitor_for(50ms);
  options.output =
      std::filesystem::path(testing::TempDir()) / "netft-monitor-missing-parent" / "data.ndjson";
  test::FakeClock clock;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  test::expect_app_error(
      ExitCode::Io, [&] { run_monitor(options, backend, output.context(), interrupt, clock); });

  EXPECT_EQ(backend.discover_calls(), 0U);
  EXPECT_EQ(backend.open_calls(), 0U);
}

TEST(MonitorCommand, RejectsUnsupportedFormatBeforeOutputOrNetwork) {
  test::FakeBackend backend;
  auto options = test::monitor_for(50ms);
  options.format = OutputFormat::Json;
  options.output =
      std::filesystem::path(testing::TempDir()) / "netft-monitor-invalid-format" / "data.json";
  test::FakeClock clock;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  test::expect_app_error(
      ExitCode::Usage, [&] { run_monitor(options, backend, output.context(), interrupt, clock); });

  EXPECT_EQ(backend.discover_calls(), 0U);
  EXPECT_EQ(backend.open_calls(), 0U);
}

TEST(MonitorCommand, MapsMissingFirstSampleToStreamFailureAndStopsSession) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  auto options = test::monitor_for(50ms);
  options.connection.timeout = 1ms;
  test::FakeClock clock;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  test::expect_app_error(
      ExitCode::Stream, [&] { run_monitor(options, backend, output.context(), interrupt, clock); });

  EXPECT_EQ(backend.session().stop_calls(), 1U);
  EXPECT_TRUE(clock.deadlines().empty());
}

TEST(MonitorCommand, MapsTransportFaultToStreamFailure) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  auto fault = test::health();
  fault.state = netft::ClientState::Faulted;
  fault.fault_code = netft::FaultCode::Timeout;
  backend.session().set_health(fault);
  auto options = test::monitor_for(50ms);
  options.connection.timeout = 1ms;
  test::FakeClock clock;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  test::expect_app_error(
      ExitCode::Stream, [&] { run_monitor(options, backend, output.context(), interrupt, clock); });

  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(MonitorCommand, MapsSeriousDeviceStatusToSensorFailure) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  auto fault = test::health();
  fault.state = netft::ClientState::Faulted;
  fault.fault_code = netft::FaultCode::SeriousStatus;
  backend.session().set_health(fault);
  auto options = test::monitor_for(50ms);
  options.connection.timeout = 1ms;
  test::FakeClock clock;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  test::expect_app_error(
      ExitCode::Sensor, [&] { run_monitor(options, backend, output.context(), interrupt, clock); });

  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(MonitorCommand, MapsSessionStartFailureToStreamAndStillStopsSession) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().fail_start();
  test::FakeClock clock;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  test::expect_app_error(ExitCode::Stream, [&] {
    run_monitor(test::monitor_for(50ms), backend, output.context(), interrupt, clock);
  });

  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(MonitorCommand, MapsOutputFailureToIoAndStopsSession) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_samples({test::sample(13)});
  backend.session().set_health(test::health());
  test::FakeClock clock;
  std::istringstream input;
  std::ostringstream standard_output;
  std::ostringstream standard_error;
  standard_output.setstate(std::ios::badbit);
  OutputContext output{input, standard_output, standard_error, false, false};
  InterruptFlag interrupt;

  test::expect_app_error(ExitCode::Io, [&] {
    run_monitor(test::monitor_for(50ms), backend, output, interrupt, clock);
  });

  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(MonitorCommand, InterruptionReturnsStatus130StopsSessionAndDoesNotRenderPartialRecord) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_samples({test::sample(14)});
  backend.session().set_health(test::health());
  auto options = test::monitor_for(5s);
  options.duration.reset();
  test::FakeClock clock;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  clock.set_sleep_hook([&](std::size_t) { interrupt.request(); });

  EXPECT_EQ(run_monitor(options, backend, output.context(), interrupt, clock),
            static_cast<int>(ExitCode::Interrupted));

  EXPECT_TRUE(output.standard_output_text().empty());
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  EXPECT_EQ(clock.deadlines().size(), 1U);
}

} // namespace
} // namespace netft_cli
