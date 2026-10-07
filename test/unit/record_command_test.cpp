#include "commands/record.hpp"

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
#include <string>

namespace netft_cli {
namespace {

using namespace std::chrono_literals;

class FixedCommandWallClock final : public WallClock {
public:
  [[nodiscard]] TimePoint now() const override { return TimePoint{1'000s}; }
};

RecordOptions record_options(const std::filesystem::path &path) {
  RecordOptions options;
  options.connection = test::connection_options();
  options.output = path;
  options.count = 1;
  return options;
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

void remove_recording(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::remove(path, error);
  std::filesystem::remove(path.string() + ".partial", error);
  std::filesystem::remove(path.string() + ".metadata.json", error);
  std::filesystem::remove(path.string() + ".metadata.json.partial", error);
}

TEST(RecordCommand, SelectsCsvFromExtensionAndHonorsQuietMode) {
  const auto path = std::filesystem::path(testing::TempDir()) / "netft-record-command.csv";
  remove_recording(path);
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_samples({test::sample(1)});
  auto options = record_options(path);
  options.terminal.verbosity = Verbosity::Quiet;
  test::FakeClock clock;
  FixedCommandWallClock wall_clock;
  NativeFilesystem filesystem;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  EXPECT_EQ(
      run_record(options, backend, output.context(), interrupt, clock, wall_clock, filesystem), 0);

  const auto table = test::parse_csv(read_file(path));
  EXPECT_EQ(table.row_count(), 1U);
  EXPECT_TRUE(table.has_columns({"schema_version", "timestamp_utc", "rdt_sequence"}));
  EXPECT_TRUE(output.standard_output_text().empty());
  EXPECT_TRUE(output.standard_error_text().empty());
  remove_recording(path);
}

TEST(RecordCommand, ExplicitNdjsonFormatDoesNotDependOnExtension) {
  const auto path = std::filesystem::path(testing::TempDir()) / "netft-record-command.data";
  remove_recording(path);
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_samples({test::sample(2)});
  auto options = record_options(path);
  options.format = OutputFormat::Ndjson;
  test::FakeClock clock;
  FixedCommandWallClock wall_clock;
  NativeFilesystem filesystem;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  EXPECT_EQ(
      run_record(options, backend, output.context(), interrupt, clock, wall_clock, filesystem), 0);

  EXPECT_EQ(test::parse_ndjson(read_file(path)).size(), 1U);
  EXPECT_FALSE(output.standard_error_text().empty());
  remove_recording(path);
}

TEST(RecordCommand, RejectsUnsupportedAutomaticExtensionBeforeFileOrNetwork) {
  const auto path = std::filesystem::path(testing::TempDir()) / "netft-record-command.txt";
  remove_recording(path);
  test::FakeBackend backend;
  auto options = record_options(path);
  test::FakeClock clock;
  FixedCommandWallClock wall_clock;
  NativeFilesystem filesystem;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;

  test::expect_app_error(ExitCode::Usage, [&] {
    static_cast<void>(
        run_record(options, backend, output.context(), interrupt, clock, wall_clock, filesystem));
  });

  EXPECT_EQ(backend.discover_calls(), 0U);
  EXPECT_EQ(backend.open_calls(), 0U);
  EXPECT_FALSE(std::filesystem::exists(path));
}

} // namespace
} // namespace netft_cli
