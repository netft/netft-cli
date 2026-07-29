#include "output/json.hpp"
#include "support/assertions.hpp"
#include "support/records.hpp"
#include "support/structured_parsers.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <sstream>

namespace netft_cli {
namespace {

TEST(Json, EmitsFiniteTypedSampleDocument) {
  std::ostringstream stream;
  write_ndjson(stream, test::sample_record());
  const auto document = test::parse_json(stream.str());

  EXPECT_EQ(document.at("rdt_sequence"), 41);
  EXPECT_EQ(document.at("raw").size(), 6U);
  EXPECT_EQ(document.at("force").at("raw"), (test::Json::array({10, -20, 30})));
  EXPECT_EQ(document.at("force").at("value"), (test::Json::array({1.25, -2.5, 3.75})));
  EXPECT_EQ(document.at("force").at("unit"), "N");
  EXPECT_EQ(document.at("torque").at("raw"), (test::Json::array({-40, 50, -60})));
  EXPECT_EQ(document.at("torque").at("value"), (test::Json::array({-4.5, 5.25, -6.75})));
  EXPECT_EQ(document.at("torque").at("unit"), "N-mm");
  EXPECT_TRUE(document.at("elapsed_seconds").is_number_float());
  EXPECT_TRUE(document.at("lost_count").is_number_unsigned());
}

TEST(Json, EmitsOneCompleteObjectPerNdjsonLine) {
  std::ostringstream stream;
  write_ndjson(stream, test::sample_record());
  write_ndjson(stream, test::next_sample_record());
  const auto documents = test::parse_ndjson(stream.str());

  ASSERT_EQ(documents.size(), 2U);
  EXPECT_EQ(documents[0].at("rdt_sequence"), 41U);
  EXPECT_EQ(documents[1].at("rdt_sequence"), 42U);
}

TEST(Json, EmitsNestedConfigurationCalibration) {
  const ConnectionOptions options{"sensor.example", 8080, 49153,
                                  std::chrono::duration<double>{0.5}};
  std::ostringstream stream;
  write_json(stream, make_configuration_record(options, test::configuration()));
  const auto document = test::parse_json(stream.str());

  EXPECT_EQ(document.at("host"), "sensor.example");
  EXPECT_EQ(document.at("product_name"), "ATI Mini45");
  EXPECT_EQ(document.at("calibration").at("source"), "sensor");
  EXPECT_EQ(document.at("calibration").at("force").at("unit"), "N");
  EXPECT_EQ(document.at("calibration").at("torque").at("unit"), "N-mm");
}

TEST(Json, EmitsBiasBeforeAndAfterDocuments) {
  std::ostringstream stream;
  const ConnectionOptions options{"sensor.example", 8080, 49153,
                                  std::chrono::duration<double>{0.5}};
  write_json(stream, BiasRecord{make_configuration_record(options, test::configuration()),
                                test::sample_record(41), test::sample_record(42)});
  const auto document = test::parse_json(stream.str());

  EXPECT_EQ(document.at("before").at("rdt_sequence"), 41U);
  EXPECT_EQ(document.at("after").at("rdt_sequence"), 42U);
}

TEST(Json, RejectsNonfiniteValuesBeforeWritingBytes) {
  auto record = test::sample_record();
  record.scaled[3] = std::numeric_limits<double>::infinity();
  std::ostringstream stream;

  test::expect_app_error(ExitCode::Io, [&] { write_ndjson(stream, record); });
  EXPECT_TRUE(stream.str().empty());
}

TEST(Json, EscapesStringsWithoutChangingTheirValues) {
  auto record = test::sample_record();
  record.host = "sensor\"line\nname";
  std::ostringstream stream;
  write_json(stream, record);

  EXPECT_EQ(test::parse_json(stream.str()).at("host"), record.host);
}

} // namespace
} // namespace netft_cli
