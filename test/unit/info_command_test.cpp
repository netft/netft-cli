#include "commands/info.hpp"

#include "app/error.hpp"
#include "support/assertions.hpp"
#include "support/fake_backend.hpp"
#include "support/memory_output.hpp"
#include "support/options.hpp"
#include "support/records.hpp"
#include "support/structured_parsers.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace netft_cli {
namespace {

TEST(InfoCommand, DiscoversConfigurationWithoutOpeningStream) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  test::MemoryOutput output(false);

  EXPECT_EQ(run_info(test::info_options(), backend, output.context()), 0);
  EXPECT_EQ(backend.discover_calls(), 1U);
  EXPECT_EQ(backend.open_calls(), 0U);
  EXPECT_TRUE(test::parse_json(output.standard_output_text()).contains("calibration"));
}

TEST(InfoCommand, UsesTextForAutomaticFormatOnTerminalOutput) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  auto options = test::info_options();
  options.format = OutputFormat::Automatic;
  test::MemoryOutput output(true);

  EXPECT_EQ(run_info(options, backend, output.context()), 0);
  EXPECT_NE(output.standard_output_text().find("Sensor: ATI Mini45"), std::string::npos);
}

TEST(InfoCommand, UsesJsonForAutomaticFormatWhenOutputIsRedirected) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  auto options = test::info_options();
  options.format = OutputFormat::Automatic;
  test::MemoryOutput output(false);

  EXPECT_EQ(run_info(options, backend, output.context()), 0);
  EXPECT_TRUE(test::parse_json(output.standard_output_text()).contains("calibration"));
}

TEST(InfoCommand, UsesJsonForAutomaticFormatWhenWritingToFile) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  const auto path =
      std::filesystem::temp_directory_path() /
      ("netft-info-command-output-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
  auto options = test::info_options();
  options.format = OutputFormat::Automatic;
  options.output = path;
  test::MemoryOutput output(true);

  EXPECT_EQ(run_info(options, backend, output.context()), 0);
  std::ifstream stream(path);
  const std::string document{std::istreambuf_iterator<char>{stream},
                             std::istreambuf_iterator<char>{}};
  EXPECT_TRUE(test::parse_json(document).contains("calibration"));
  EXPECT_TRUE(output.standard_output_text().empty());
  EXPECT_TRUE(std::filesystem::remove(path));
}

TEST(InfoCommand, OpensOutputBeforeDiscoveringSensor) {
  test::FakeBackend backend;
  auto options = test::info_options();
  options.output =
      std::filesystem::path(testing::TempDir()) / "netft-info-missing-parent" / "info.json";
  test::MemoryOutput output(false);

  test::expect_app_error(ExitCode::Io, [&] { run_info(options, backend, output.context()); });
  EXPECT_EQ(backend.discover_calls(), 0U);
}

TEST(InfoCommand, MapsDiscoveryFailureToExitThree) {
  test::FakeBackend backend;
  backend.fail_discovery();
  test::MemoryOutput output(false);

  test::expect_app_error(ExitCode::Discovery,
                         [&] { run_info(test::info_options(), backend, output.context()); });
}

} // namespace
} // namespace netft_cli
