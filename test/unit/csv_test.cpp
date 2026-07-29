#include "output/csv.hpp"
#include "support/assertions.hpp"
#include "support/records.hpp"
#include "support/structured_parsers.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <sstream>

namespace netft_cli {
namespace {

TEST(Csv, WritesHeaderOnceAndSixRawAndScaledAxes) {
  std::ostringstream stream;
  CsvWriter writer(stream);
  writer.write(test::sample_record());
  writer.write(test::next_sample_record());
  const auto table = test::parse_csv(stream.str());

  EXPECT_EQ(table.header_count(), 1U);
  EXPECT_TRUE(table.has_columns({"raw_fx", "fx", "raw_tz", "tz"}));
  EXPECT_EQ(table.row_count(), 2U);
  ASSERT_EQ(table.header.size(), table.rows.front().size());
}

TEST(Csv, UsesStableFlatColumnOrder) {
  std::ostringstream stream;
  CsvWriter writer(stream);
  writer.write(test::sample_record());
  const auto table = test::parse_csv(stream.str());

  EXPECT_EQ(table.header, (std::vector<std::string>{"host",
                                                    "elapsed_seconds",
                                                    "rdt_sequence",
                                                    "ft_sequence",
                                                    "status",
                                                    "raw_fx",
                                                    "raw_fy",
                                                    "raw_fz",
                                                    "raw_tx",
                                                    "raw_ty",
                                                    "raw_tz",
                                                    "fx",
                                                    "fy",
                                                    "fz",
                                                    "tx",
                                                    "ty",
                                                    "tz",
                                                    "force_unit",
                                                    "torque_unit",
                                                    "receive_rate_hz",
                                                    "lost_count",
                                                    "duplicate_count",
                                                    "out_of_order_count",
                                                    "state"}));
  ASSERT_EQ(table.rows.size(), 1U);
  EXPECT_EQ(table.rows[0][5], "10");
  EXPECT_EQ(table.rows[0][10], "-60");
  EXPECT_EQ(table.rows[0][11], "1.25");
  EXPECT_EQ(table.rows[0][16], "-6.75");
}

TEST(Csv, QuotesTextAccordingToRfc4180) {
  auto record = test::sample_record();
  record.host = "sensor,\"line\"\r\nname";
  std::ostringstream stream;
  CsvWriter writer(stream);
  writer.write(record);
  const auto table = test::parse_csv(stream.str());

  ASSERT_EQ(table.rows.size(), 1U);
  EXPECT_EQ(table.rows[0][0], record.host);
}

TEST(Csv, RejectsNonfiniteValuesBeforeWritingHeader) {
  auto record = test::sample_record();
  record.receive_rate_hz = std::numeric_limits<double>::quiet_NaN();
  std::ostringstream stream;
  CsvWriter writer(stream);

  test::expect_app_error(ExitCode::Io, [&] { writer.write(record); });
  EXPECT_TRUE(stream.str().empty());
}

} // namespace
} // namespace netft_cli
