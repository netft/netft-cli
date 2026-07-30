#include "config/resolver.hpp"

#include "app/error.hpp"
#include "cli/parser.hpp"

#include <gtest/gtest.h>

#include <string>
#include <variant>

namespace netft_cli {
namespace {

void expect_usage_error(Action action, const EnvironmentMap &environment = {}) {
  try {
    static_cast<void>(resolve_options(std::move(action), environment));
    ADD_FAILURE() << "Expected a usage error";
  } catch (const AppError &error) {
    EXPECT_EQ(error.code(), ExitCode::Usage);
  } catch (...) {
    ADD_FAILURE() << "Expected AppError";
  }
}

TEST(ConfigResolver, CommandLineConnectionValuesOverrideEnvironment) {
  auto action = parse_arguments(
      {"info", "cli.example", "--http-port", "8080", "--rdt-port", "49153", "--timeout", "250ms"});
  const EnvironmentMap environment{{"NETFT_HOST", "env.example"},
                                   {"NETFT_HTTP_PORT", "81"},
                                   {"NETFT_RDT_PORT", "49154"},
                                   {"NETFT_TIMEOUT", "2s"}};

  const auto resolved = resolve_options(std::move(action), environment);
  const auto &info = std::get<InfoOptions>(resolved);
  EXPECT_EQ(info.connection.host, "cli.example");
  EXPECT_EQ(info.connection.http_port, 8080);
  EXPECT_EQ(info.connection.rdt_port, 49153);
  EXPECT_DOUBLE_EQ(info.connection.timeout.count(), 0.25);
}

TEST(ConfigResolver, EnvironmentFillsOmittedConnectionValues) {
  auto action = parse_arguments({"monitor"});
  const EnvironmentMap environment{{"NETFT_HOST", "env.example"},
                                   {"NETFT_HTTP_PORT", "8080"},
                                   {"NETFT_RDT_PORT", "49153"},
                                   {"NETFT_TIMEOUT", "750ms"}};

  const auto resolved = resolve_options(std::move(action), environment);
  const auto &monitor = std::get<MonitorOptions>(resolved);
  EXPECT_EQ(monitor.connection.host, "env.example");
  EXPECT_EQ(monitor.connection.http_port, 8080);
  EXPECT_EQ(monitor.connection.rdt_port, 49153);
  EXPECT_DOUBLE_EQ(monitor.connection.timeout.count(), 0.75);
}

TEST(ConfigResolver, KeepsBuiltInConnectionDefaults) {
  auto action = parse_arguments({"info", "sensor.example"});

  const auto resolved = resolve_options(std::move(action), {});
  const auto &info = std::get<InfoOptions>(resolved);
  EXPECT_EQ(info.connection.http_port, 80);
  EXPECT_EQ(info.connection.rdt_port, 49152);
  EXPECT_DOUBLE_EQ(info.connection.timeout.count(), 1.0);
}

TEST(ConfigResolver, RejectsMissingHostAndInvalidEnvironmentValues) {
  expect_usage_error(parse_arguments({"info"}));
  expect_usage_error(parse_arguments({"info"}), {{"NETFT_HOST", "http://sensor.example"}});
  expect_usage_error(parse_arguments({"info", "sensor.example"}), {{"NETFT_HTTP_PORT", "0"}});
  expect_usage_error(parse_arguments({"info", "sensor.example"}), {{"NETFT_RDT_PORT", "65536"}});
  expect_usage_error(parse_arguments({"info", "sensor.example"}), {{"NETFT_TIMEOUT", "invalid"}});
}

TEST(ConfigResolver, AppliesColorAndVerbosityRules) {
  auto automatic = resolve_options(parse_arguments({"info", "sensor.example"}), {});
  EXPECT_EQ(std::get<InfoOptions>(automatic).terminal.color, ColorMode::Automatic);

  auto no_color = resolve_options(parse_arguments({"info", "sensor.example"}), {{"NO_COLOR", ""}});
  EXPECT_EQ(std::get<InfoOptions>(no_color).terminal.color, ColorMode::Never);

  auto explicit_color =
      resolve_options(parse_arguments({"--color", "always", "--verbose", "info", "sensor.example"}),
                      {{"NO_COLOR", "1"}});
  const auto &verbose = std::get<InfoOptions>(explicit_color).terminal;
  EXPECT_EQ(verbose.color, ColorMode::Always);
  EXPECT_EQ(verbose.verbosity, Verbosity::Verbose);

  auto quiet = resolve_options(
      parse_arguments({"info", "sensor.example", "--quiet", "--format", "text"}), {});
  EXPECT_EQ(std::get<InfoOptions>(quiet).terminal.verbosity, Verbosity::Quiet);
}

TEST(ConfigResolver, MachineFormatsAlwaysDisableColor) {
  auto action = resolve_options(
      parse_arguments({"--color", "always", "info", "sensor.example", "--format", "json"}), {});

  EXPECT_EQ(std::get<InfoOptions>(action).terminal.color, ColorMode::Never);
}

TEST(ConfigResolver, RejectsConflictingTerminalModes) {
  try {
    static_cast<void>(parse_arguments({"--verbose", "--quiet", "info", "sensor.example"}));
    ADD_FAILURE() << "Expected a usage error";
  } catch (const AppError &error) {
    EXPECT_EQ(error.code(), ExitCode::Usage);
  }
}

} // namespace
} // namespace netft_cli
