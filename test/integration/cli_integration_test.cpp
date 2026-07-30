#include "application.hpp"
#include "integration/fake_sensor.hpp"
#include "output/confirmation.hpp"
#include "sensor/netft_backend.hpp"
#include "support/structured_parsers.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace netft_cli::test {
namespace {

class AcceptConfirmation final : public Confirmation {
public:
  bool confirm(const BiasPreview &) override { return true; }
};

class IntegrationEnvironment final : public AppEnvironment {
public:
  SensorBackend &backend() override { return backend_; }
  OutputContext &output() override { return output_; }
  Confirmation &confirmation() override { return confirmation_; }
  InterruptFlag &interrupt() override { return interrupt_; }
  Clock &clock() override { return clock_; }
  WallClock &wall_clock() override { return wall_clock_; }
  Filesystem &filesystem() override { return filesystem_; }
  EnvironmentMap environment() const override { return environment_; }

  int show_help(const ShowHelp &) override {
    standard_output_ << "help\n";
    return 0;
  }

  int show_version() override {
    standard_output_ << "0.1.0\n";
    return 0;
  }

  std::string stdout_text() const { return standard_output_.str(); }
  std::string stderr_text() const { return standard_error_.str(); }
  const TerminalOptions &terminal_options() const { return output_.terminal; }
  void set_environment(EnvironmentMap environment) { environment_ = std::move(environment); }

private:
  std::istringstream input_;
  std::ostringstream standard_output_;
  std::ostringstream standard_error_;
  OutputContext output_{input_, standard_output_, standard_error_, false, false, {}};
  NetftBackend backend_;
  AcceptConfirmation confirmation_;
  InterruptFlag interrupt_;
  SystemClock clock_;
  SystemWallClock wall_clock_;
  NativeFilesystem filesystem_;
  EnvironmentMap environment_;
};

std::string port_text(int port) { return std::to_string(port); }

std::vector<std::string> connection_arguments(std::string command, const FakeSensor &sensor) {
  return {std::move(command), sensor.host(),
          "--http-port",      port_text(sensor.http_port()),
          "--rdt-port",       port_text(sensor.rdt_port()),
          "--timeout",        "500ms"};
}

int run_cli(const std::vector<std::string> &arguments, IntegrationEnvironment &environment) {
  std::vector<std::string_view> views;
  views.reserve(arguments.size());
  for (const auto &argument : arguments) {
    views.push_back(argument);
  }
  return run_application(views, environment);
}

TEST(CliIntegration, InfoUsesHttpConfigurationAndNeverStartsRdt) {
  FakeSensor sensor;
  IntegrationEnvironment environment;
  auto arguments = connection_arguments("info", sensor);
  arguments.insert(arguments.end(), {"--format", "json"});

  ASSERT_EQ(run_cli(arguments, environment), 0);
  const auto document = parse_json(environment.stdout_text());
  EXPECT_EQ(document.at("host"), sensor.host());
  EXPECT_EQ(document.at("http_port"), sensor.http_port());
  EXPECT_EQ(document.at("calibration").at("source"), "sensor");
  EXPECT_GE(sensor.http_request_count(), 1U);
  EXPECT_EQ(sensor.start_realtime_count(), 0U);
  EXPECT_TRUE(environment.stderr_text().empty());
}

TEST(CliIntegration, BiasUsesPreviewAndOneShotBiasedSession) {
  FakeSensor sensor;
  IntegrationEnvironment environment;
  auto arguments = connection_arguments("bias", sensor);
  arguments.insert(arguments.end(), {"--yes", "--format", "json"});

  ASSERT_EQ(run_cli(arguments, environment), 0);
  const auto document = parse_json(environment.stdout_text());
  EXPECT_TRUE(document.at("before").at("raw").is_array());
  EXPECT_TRUE(document.at("after").at("raw").is_array());
  EXPECT_EQ(sensor.software_bias_count(), 1U);
  EXPECT_GE(sensor.start_realtime_count(), 2U);
  EXPECT_TRUE(sensor.wait_for_stop_streaming(2));
  EXPECT_TRUE(environment.stderr_text().empty());
}

TEST(CliIntegration, CheckProducesOneTypedResultAndStopsStreaming) {
  FakeSensor sensor;
  IntegrationEnvironment environment;
  auto arguments = connection_arguments("check", sensor);
  arguments.insert(arguments.end(), {"--duration", "100ms", "--format", "json"});

  ASSERT_EQ(run_cli(arguments, environment), 0);
  const auto document = parse_json(environment.stdout_text());
  EXPECT_EQ(document.at("schema_version"), 1);
  EXPECT_EQ(document.at("result"), "pass");
  EXPECT_TRUE(document.at("checks").is_array());
  EXPECT_TRUE(sensor.wait_for_stop_streaming());
  EXPECT_TRUE(environment.stderr_text().empty());
}

TEST(CliIntegration, RecordWritesEveryAcceptedSampleAndFinalizesFile) {
  const auto path = std::filesystem::path(testing::TempDir()) / "netft-cli-integration.ndjson";
  std::error_code error;
  std::filesystem::remove(path, error);
  std::filesystem::remove(path.string() + ".partial", error);
  FakeSensor sensor;
  IntegrationEnvironment environment;
  auto arguments = connection_arguments("record", sensor);
  arguments.insert(arguments.end(),
                   {"--output", path.string(), "--count", "5", "--format", "ndjson", "--quiet"});

  ASSERT_EQ(run_cli(arguments, environment), 0);
  std::ifstream stream(path, std::ios::binary);
  const std::string contents{std::istreambuf_iterator<char>{stream},
                             std::istreambuf_iterator<char>{}};
  const auto documents = parse_ndjson(contents);
  EXPECT_EQ(documents.size(), 5U);
  EXPECT_TRUE(sensor.wait_for_stop_streaming());
  EXPECT_FALSE(std::filesystem::exists(path.string() + ".partial"));
  EXPECT_TRUE(environment.stdout_text().empty());
  EXPECT_TRUE(environment.stderr_text().empty());
  EXPECT_TRUE(std::filesystem::remove(path));
}

TEST(CliIntegration, ResolvesEnvironmentAndPropagatesTerminalOptions) {
  FakeSensor sensor;
  IntegrationEnvironment environment;
  environment.set_environment({{"NETFT_HOST", sensor.host()},
                               {"NETFT_HTTP_PORT", port_text(sensor.http_port())},
                               {"NETFT_RDT_PORT", port_text(sensor.rdt_port())},
                               {"NETFT_TIMEOUT", "500ms"}});

  ASSERT_EQ(run_cli({"--verbose", "--color", "always", "info"}, environment), 0);
  EXPECT_EQ(environment.terminal_options().verbosity, Verbosity::Verbose);
  EXPECT_EQ(environment.terminal_options().color, ColorMode::Always);
  EXPECT_GE(sensor.http_request_count(), 1U);
}

} // namespace
} // namespace netft_cli::test
