#include "app/error.hpp"
#include "cli/options.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace netft_cli {
namespace {

std::vector<std::string_view> arguments(std::initializer_list<std::string_view> values) {
  return {values};
}

void expect_usage_error(std::initializer_list<std::string_view> values) {
  try {
    static_cast<void>(parse_arguments(arguments(values)));
    ADD_FAILURE() << "Expected a usage error";
  } catch (const AppError &error) {
    EXPECT_EQ(error.code(), ExitCode::Usage);
  } catch (...) {
    ADD_FAILURE() << "Expected AppError";
  }
}

TEST(Options, ParsesPositionalMonitorHostAndRate) {
  const auto action = parse_arguments({"monitor", "192.168.1.1", "--rate", "25"});
  const auto &monitor = std::get<MonitorOptions>(action);

  EXPECT_EQ(monitor.connection.host, "192.168.1.1");
  EXPECT_DOUBLE_EQ(monitor.rate_hz, 25.0);
  EXPECT_EQ(monitor.format, OutputFormat::Automatic);
}

TEST(Options, ParsesConnectionAndOutputOptions) {
  const auto action =
      parse_arguments({"info", "sensor.local", "--http-port", "8080", "--rdt-port", "49153",
                       "--timeout", "250ms", "--format", "json", "--output", "reading.json"});
  const auto &info = std::get<InfoOptions>(action);

  EXPECT_EQ(info.connection.host, "sensor.local");
  EXPECT_EQ(info.connection.http_port, 8080);
  EXPECT_EQ(info.connection.rdt_port, 49153);
  EXPECT_DOUBLE_EQ(info.connection.timeout.count(), 0.25);
  EXPECT_EQ(info.format, OutputFormat::Json);
  ASSERT_TRUE(info.output.has_value());
  EXPECT_EQ(*info.output, "reading.json");
}

TEST(Options, ParsesMonitorDuration) {
  const auto action = parse_arguments({"monitor", "sensor.local", "--duration", "1.5s"});
  const auto &monitor = std::get<MonitorOptions>(action);

  ASSERT_TRUE(monitor.duration.has_value());
  EXPECT_DOUBLE_EQ(monitor.duration->count(), 1.5);
}

TEST(Options, ParsesBiasYes) {
  const auto action = parse_arguments({"bias", "sensor.local", "--yes"});

  EXPECT_TRUE(std::get<BiasOptions>(action).assume_yes);
}

TEST(Options, RequiresYesForNoninteractiveBiasAtRuntimeNotParseTime) {
  const auto action = parse_arguments({"bias", "192.168.1.1"});

  EXPECT_FALSE(std::get<BiasOptions>(action).assume_yes);
}

TEST(Options, AcceptsEachInfoFormat) {
  for (const auto &[name, expected] :
       {std::pair{"auto", OutputFormat::Automatic}, std::pair{"text", OutputFormat::Text},
        std::pair{"json", OutputFormat::Json}}) {
    const auto action = parse_arguments({"info", "sensor.local", "--format", name});
    EXPECT_EQ(std::get<InfoOptions>(action).format, expected);
  }
}

TEST(Options, AcceptsEachMonitorFormat) {
  for (const auto &[name, expected] :
       {std::pair{"auto", OutputFormat::Automatic}, std::pair{"table", OutputFormat::Table},
        std::pair{"ndjson", OutputFormat::Ndjson}, std::pair{"csv", OutputFormat::Csv}}) {
    const auto action = parse_arguments({"monitor", "sensor.local", "--format", name});
    EXPECT_EQ(std::get<MonitorOptions>(action).format, expected);
  }
}

TEST(Options, AcceptsEachBiasFormat) {
  for (const auto &[name, expected] :
       {std::pair{"auto", OutputFormat::Automatic}, std::pair{"text", OutputFormat::Text},
        std::pair{"json", OutputFormat::Json}}) {
    const auto action = parse_arguments({"bias", "sensor.local", "--format", name});
    EXPECT_EQ(std::get<BiasOptions>(action).format, expected);
  }
}

TEST(Options, RejectsFormatNotSupportedByCommand) {
  expect_usage_error({"info", "sensor.local", "--format", "csv"});
  expect_usage_error({"monitor", "sensor.local", "--format", "json"});
  expect_usage_error({"bias", "sensor.local", "--format", "table"});
}

TEST(Options, AcceptsPortRangeEndpoints) {
  const auto action =
      parse_arguments({"info", "sensor.local", "--http-port", "1", "--rdt-port", "65535"});
  const auto &info = std::get<InfoOptions>(action);

  EXPECT_EQ(info.connection.http_port, 1);
  EXPECT_EQ(info.connection.rdt_port, 65535);
}

TEST(Options, RejectsPortsOutsideRange) {
  expect_usage_error({"info", "sensor.local", "--http-port", "0"});
  expect_usage_error({"info", "sensor.local", "--rdt-port", "65536"});
}

TEST(Options, RejectsNonpositiveOrNonfiniteRateAndTimeout) {
  expect_usage_error({"monitor", "sensor.local", "--rate", "0"});
  expect_usage_error({"monitor", "sensor.local", "--rate", "nan"});
  expect_usage_error({"monitor", "sensor.local", "--timeout", "0s"});
  expect_usage_error({"monitor", "sensor.local", "--timeout", "infs"});
}

TEST(Options, RejectsMalformedDuration) {
  expect_usage_error({"monitor", "sensor.local", "--duration", "1"});
  expect_usage_error({"monitor", "sensor.local", "--duration", "1m"});
  expect_usage_error({"monitor", "sensor.local", "--duration", "0s"});
  expect_usage_error({"monitor", "sensor.local", "--duration", "-1s"});
  expect_usage_error({"monitor", "sensor.local", "--duration", "nans"});
}

TEST(Options, RejectsUrlInsteadOfHost) {
  expect_usage_error({"info", "http://192.168.1.1/config.xml"});
}

TEST(Options, RejectsDuplicateHostAndUnknownOptions) {
  expect_usage_error({"info", "sensor.local", "other.local"});
  expect_usage_error({"info", "sensor.local", "--verbose"});
}

TEST(Options, ProducesGeneralAndCommandHelpTopics) {
  const auto general = parse_arguments({"--help"});
  const auto monitor = parse_arguments({"monitor", "--help"});
  const auto info = parse_arguments({"help", "info"});

  EXPECT_EQ(std::get<ShowHelp>(general).topic, "general");
  EXPECT_EQ(std::get<ShowHelp>(monitor).topic, "monitor");
  EXPECT_EQ(std::get<ShowHelp>(info).topic, "info");
}

TEST(Options, ProducesVersionAction) {
  EXPECT_TRUE(std::holds_alternative<ShowVersion>(parse_arguments({"--version"})));
}

} // namespace
} // namespace netft_cli
