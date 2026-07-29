#include "application.hpp"
#include "integration/fake_sensor.hpp"
#include "output/confirmation.hpp"
#include "sensor/netft_backend.hpp"
#include "support/structured_parsers.hpp"

#include <gtest/gtest.h>

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

private:
  std::istringstream input_;
  std::ostringstream standard_output_;
  std::ostringstream standard_error_;
  OutputContext output_{input_, standard_output_, standard_error_, false, false};
  NetftBackend backend_;
  AcceptConfirmation confirmation_;
  InterruptFlag interrupt_;
  SystemClock clock_;
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

TEST(CliIntegration, MonitorEmitsRecordsAndStopsRdtAfterBoundedRun) {
  FakeSensor sensor;
  IntegrationEnvironment environment;
  auto arguments = connection_arguments("monitor", sensor);
  arguments.insert(arguments.end(), {"--duration", "100ms", "--rate", "50", "--format", "ndjson"});

  ASSERT_EQ(run_cli(arguments, environment), 0);
  const auto documents = parse_ndjson(environment.stdout_text());
  ASSERT_FALSE(documents.empty());
  EXPECT_EQ(documents.front().at("host"), sensor.host());
  EXPECT_TRUE(documents.front().at("raw").is_array());
  EXPECT_TRUE(sensor.wait_for_stop_streaming());
  EXPECT_GE(sensor.start_realtime_count(), 1U);
  EXPECT_GE(sensor.stop_streaming_count(), 1U);
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

} // namespace
} // namespace netft_cli::test
