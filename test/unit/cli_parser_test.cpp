#include "cli/parser.hpp"

#include "app/error.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <variant>
#include <vector>

namespace netft_cli {
namespace {

void expect_usage_error(std::initializer_list<std::string_view> values) {
  try {
    static_cast<void>(parse_arguments(std::vector<std::string_view>{values}));
    ADD_FAILURE() << "Expected a usage error";
  } catch (const AppError &error) {
    EXPECT_EQ(error.code(), ExitCode::Usage);
  } catch (...) {
    ADD_FAILURE() << "Expected AppError";
  }
}

TEST(CliParser, PreservesExistingTypedInvocations) {
  const auto info = std::get<InfoOptions>(
      parse_arguments({"info", "sensor.example", "--http-port", "8080", "--format", "json"}));
  EXPECT_EQ(info.connection.host, "sensor.example");
  EXPECT_EQ(info.connection.http_port, 8080);
  EXPECT_EQ(info.format, OutputFormat::Json);

  const auto monitor = std::get<MonitorOptions>(
      parse_arguments({"monitor", "sensor.example", "--rate", "25", "--duration", "1.5s"}));
  EXPECT_DOUBLE_EQ(monitor.rate_hz, 25.0);
  ASSERT_TRUE(monitor.duration.has_value());
  EXPECT_DOUBLE_EQ(monitor.duration->count(), 1.5);

  const auto bias = std::get<BiasOptions>(parse_arguments({"bias", "sensor.example", "--yes"}));
  EXPECT_TRUE(bias.assume_yes);
}

TEST(CliParser, ParsesCheckCriteriaIntoTypedOptions) {
  const auto check = std::get<CheckOptions>(
      parse_arguments({"check", "sensor.example", "--duration", "10s", "--min-rate", "1000",
                       "--max-loss", "0.1", "--max-reconnects", "0", "--format", "json"}));

  EXPECT_EQ(check.connection.host, "sensor.example");
  EXPECT_DOUBLE_EQ(check.duration.count(), 10.0);
  ASSERT_TRUE(check.min_rate_hz.has_value());
  EXPECT_DOUBLE_EQ(*check.min_rate_hz, 1000.0);
  ASSERT_TRUE(check.max_loss_percent.has_value());
  EXPECT_DOUBLE_EQ(*check.max_loss_percent, 0.1);
  ASSERT_TRUE(check.max_reconnects.has_value());
  EXPECT_EQ(*check.max_reconnects, 0U);
  EXPECT_EQ(check.format, OutputFormat::Json);
}

TEST(CliParser, ParsesRecordLimitsAndExplicitFormat) {
  const auto record = std::get<RecordOptions>(
      parse_arguments({"record", "sensor.example", "--output", "capture.ndjson", "--format",
                       "ndjson", "--duration", "2s", "--count", "100000"}));

  EXPECT_EQ(record.connection.host, "sensor.example");
  EXPECT_EQ(record.output, "capture.ndjson");
  EXPECT_EQ(record.format, OutputFormat::Ndjson);
  ASSERT_TRUE(record.duration.has_value());
  EXPECT_DOUBLE_EQ(record.duration->count(), 2.0);
  ASSERT_TRUE(record.count.has_value());
  EXPECT_EQ(*record.count, UINT64_C(100000));
}

TEST(CliParser, ParsesEachCompletionShell) {
  for (const auto &[name, shell] :
       {std::pair{"bash", CompletionShell::Bash}, std::pair{"zsh", CompletionShell::Zsh},
        std::pair{"fish", CompletionShell::Fish},
        std::pair{"powershell", CompletionShell::PowerShell}}) {
    const auto completion = std::get<CompletionOptions>(parse_arguments({"completion", name}));
    EXPECT_EQ(completion.shell, shell);
  }
}

TEST(CliParser, ProducesGeneralAndEveryCommandHelpTopic) {
  EXPECT_EQ(std::get<ShowHelp>(parse_arguments({"--help"})).topic, "general");
  for (const auto topic : {"info", "monitor", "check", "record", "bias", "completion"}) {
    EXPECT_EQ(std::get<ShowHelp>(parse_arguments({"help", topic})).topic, topic);
    EXPECT_EQ(std::get<ShowHelp>(parse_arguments({topic, "--help"})).topic, topic);
  }
  EXPECT_TRUE(std::holds_alternative<ShowVersion>(parse_arguments({"--version"})));
}

TEST(CliParser, RejectsInvalidCommandOptionsAndPositionals) {
  expect_usage_error({"unknown", "sensor.example"});
  expect_usage_error({"info", "sensor.example", "--rate", "2"});
  expect_usage_error({"record", "sensor.example"});
  expect_usage_error({"completion"});
  expect_usage_error({"completion", "bash", "extra"});
  expect_usage_error({"completion", "unknown"});
  expect_usage_error({"info", "sensor.example", "other.example"});
  expect_usage_error({"monitor", "sensor.example", "--rate", "2", "--rate", "3"});
}

TEST(CliParser, RejectsInvalidNumericBounds) {
  expect_usage_error({"check", "sensor.example", "--min-rate", "0"});
  expect_usage_error({"check", "sensor.example", "--max-loss", "-0.1"});
  expect_usage_error({"check", "sensor.example", "--max-loss", "100.1"});
  expect_usage_error({"check", "sensor.example", "--max-reconnects", "-1"});
  expect_usage_error({"check", "sensor.example", "--max-reconnects", "1.5"});
  expect_usage_error({"record", "sensor.example", "--output", "capture.csv", "--count", "0"});
  expect_usage_error(
      {"record", "sensor.example", "--output", "capture.csv", "--count", "18446744073709551616"});
}

TEST(CliParser, EnforcesCommandOutputFormats) {
  expect_usage_error({"check", "sensor.example", "--format", "csv"});
  expect_usage_error({"record", "sensor.example", "--output", "capture.csv", "--format", "json"});
  EXPECT_TRUE(std::holds_alternative<RecordOptions>(
      parse_arguments({"record", "sensor.example", "--output", "capture.csv"})));
}

} // namespace
} // namespace netft_cli
