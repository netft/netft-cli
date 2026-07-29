#include "output/context.hpp"
#include "output/records.hpp"
#include "support/records.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace netft_cli {
namespace {

TEST(Records, ConstructsConfigurationFromDeviceDiscovery) {
  const ConnectionOptions options{"sensor.example", 8080, 49153,
                                  std::chrono::duration<double>{0.5}};
  const auto record = make_configuration_record(options, test::configuration());

  EXPECT_EQ(record.host, "sensor.example");
  EXPECT_EQ(record.http_port, 8080);
  EXPECT_EQ(record.rdt_port, 49153);
  EXPECT_EQ(record.product_name, "ATI Mini45");
  EXPECT_EQ(record.force_unit, "N");
  EXPECT_EQ(record.torque_unit, "N-mm");
  EXPECT_DOUBLE_EQ(record.counts_per_force_unit, 1'000'000.0);
  EXPECT_DOUBLE_EQ(record.counts_per_torque_unit, 1'000'000.0);
  EXPECT_EQ(record.calibration_source, "sensor");
  EXPECT_EQ(record.configuration_revision, 7U);
}

TEST(Records, ConstructsScaledAxesInFxFyFzTxTyTzOrder) {
  const auto record = test::sample_record();

  EXPECT_EQ(record.host, "192.168.1.1");
  EXPECT_DOUBLE_EQ(record.elapsed_seconds, 1.25);
  EXPECT_EQ(record.raw, (std::array<std::int32_t, 6>{10, -20, 30, -40, 50, -60}));
  EXPECT_EQ(record.scaled, (std::array<double, 6>{1.25, -2.5, 3.75, -4.5, 5.25, -6.75}));
  EXPECT_EQ(record.force_unit, "N");
  EXPECT_EQ(record.torque_unit, "N-mm");
  EXPECT_DOUBLE_EQ(record.receive_rate_hz, 7000.5);
  EXPECT_EQ(record.lost_count, 3U);
  EXPECT_EQ(record.duplicate_count, 4U);
  EXPECT_EQ(record.out_of_order_count, 5U);
  EXPECT_EQ(record.state, "streaming");
}

TEST(OutputHandle, ReferencesStandardOutputWithoutOwningIt) {
  std::ostringstream stream;
  auto output = OutputHandle::standard(stream);
  output.stream() << "data";
  output.flush();

  EXPECT_EQ(stream.str(), "data");
}

TEST(OutputHandle, OwnsBinaryModeFileAndFlushesData) {
  const auto path = std::filesystem::temp_directory_path() / "netft-output-handle-test.bin";
  {
    auto output = OutputHandle::file(path);
    output.stream().write("a\r\nb", 4);
    output.flush();
  }

  std::ifstream input(path, std::ios::binary);
  const std::string contents{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
  std::filesystem::remove(path);
  EXPECT_EQ(contents, "a\r\nb");
}

} // namespace
} // namespace netft_cli
