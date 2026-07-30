#include "recording/output_file.hpp"

#include "app/error.hpp"
#include "support/assertions.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace netft_cli {
namespace {

class RenameFailureFilesystem final : public Filesystem {
public:
  [[nodiscard]] bool exists(const std::filesystem::path &path) const override {
    return std::filesystem::exists(path);
  }

  void rename(const std::filesystem::path &, const std::filesystem::path &) override {
    throw std::runtime_error("injected rename failure");
  }
};

std::filesystem::path output_path(std::string_view name) {
  const auto directory = std::filesystem::path(testing::TempDir()) / "netft-output-file";
  std::filesystem::create_directories(directory);
  return directory / name;
}

void remove_output(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::remove(path, error);
  std::filesystem::remove(path.string() + ".partial", error);
}

TEST(OutputFile, FinalizesPartialFileWithoutReplacingDestination) {
  const auto path = output_path("complete.csv");
  remove_output(path);
  NativeFilesystem filesystem;
  {
    OutputFile output(path, filesystem);
    output.stream() << "complete";
    output.finalize();
    EXPECT_TRUE(output.finalized());
  }

  EXPECT_TRUE(std::filesystem::is_regular_file(path));
  EXPECT_FALSE(std::filesystem::exists(path.string() + ".partial"));
  std::ifstream input(path);
  std::string contents;
  input >> contents;
  EXPECT_EQ(contents, "complete");
  remove_output(path);
}

TEST(OutputFile, RejectsExistingDestinationBeforeOpeningPartial) {
  const auto path = output_path("existing.csv");
  remove_output(path);
  std::ofstream(path) << "existing";
  NativeFilesystem filesystem;

  test::expect_app_error(ExitCode::Io, [&] { OutputFile output(path, filesystem); });

  EXPECT_FALSE(std::filesystem::exists(path.string() + ".partial"));
  remove_output(path);
}

TEST(OutputFile, RetainsPartialFileWhenStreamOrRenameFails) {
  const auto write_path = output_path("write-failure.csv");
  remove_output(write_path);
  NativeFilesystem native;
  {
    OutputFile output(write_path, native);
    output.stream() << "partial";
    output.stream().setstate(std::ios::badbit);
    test::expect_app_error(ExitCode::Io, [&] { output.finalize(); });
  }
  EXPECT_TRUE(std::filesystem::is_regular_file(write_path.string() + ".partial"));
  EXPECT_FALSE(std::filesystem::exists(write_path));
  remove_output(write_path);

  const auto rename_path = output_path("rename-failure.csv");
  remove_output(rename_path);
  RenameFailureFilesystem failure;
  {
    OutputFile output(rename_path, failure);
    output.stream() << "partial";
    test::expect_app_error(ExitCode::Io, [&] { output.finalize(); });
  }
  EXPECT_TRUE(std::filesystem::is_regular_file(rename_path.string() + ".partial"));
  EXPECT_FALSE(std::filesystem::exists(rename_path));
  remove_output(rename_path);
}

} // namespace
} // namespace netft_cli
