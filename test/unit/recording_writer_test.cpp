#include "recording/csv_writer.hpp"
#include "recording/ndjson_writer.hpp"
#include "recording/writer.hpp"

#include "support/records.hpp"
#include "support/structured_parsers.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <sstream>

namespace netft_cli {
namespace {

RecordingRecord recording_record() {
  const auto timestamp =
      WallClock::TimePoint{std::chrono::seconds{1'767'230'645} + std::chrono::milliseconds{123}};
  return make_recording_record(
      test::sample(), std::chrono::steady_clock::time_point{std::chrono::milliseconds{1000}},
      timestamp);
}

TEST(RecordingWriter, CsvContainsVersionedTimestampedSampleContract) {
  std::ostringstream stream;
  CsvRecordingWriter writer(stream);
  writer.write(recording_record());
  const auto table = test::parse_csv(stream.str());

  EXPECT_TRUE(table.has_columns({"schema_version", "timestamp_utc", "elapsed_seconds",
                                 "rdt_sequence", "raw_fx", "tz", "torque_unit"}));
  ASSERT_EQ(table.rows.size(), 1U);
  EXPECT_EQ(table.rows[0][0], "1");
  EXPECT_EQ(table.rows[0][1], "2026-01-01T01:24:05.123Z");
}

TEST(RecordingWriter, NdjsonContainsVersionedTimestampedTypedSample) {
  std::ostringstream stream;
  NdjsonRecordingWriter writer(stream);
  writer.write(recording_record());
  const auto documents = test::parse_ndjson(stream.str());

  ASSERT_EQ(documents.size(), 1U);
  const auto &document = documents.front();
  EXPECT_EQ(document.at("schema_version"), 1);
  EXPECT_EQ(document.at("timestamp_utc"), "2026-01-01T01:24:05.123Z");
  EXPECT_EQ(document.at("sample").at("rdt_sequence"), 41);
  EXPECT_EQ(document.at("sample").at("raw").size(), 6U);
  EXPECT_EQ(document.at("sample").at("force").at("unit"), "N");
  EXPECT_EQ(document.at("sample").at("torque").at("unit"), "N-mm");
}

} // namespace
} // namespace netft_cli
