#include "recording/ndjson_writer.hpp"

#include "app/error.hpp"
#include "output/records.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>

namespace netft_cli {
namespace {

void require_finite(double value) {
  if (!std::isfinite(value)) {
    throw AppError{ExitCode::Io, "non-finite recording value"};
  }
}

template <typename Value> void append_number(std::string &output, Value value) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  if constexpr (std::is_floating_point_v<Value>) {
    stream << std::setprecision(std::numeric_limits<Value>::max_digits10);
  }
  stream << value;
  output += stream.str();
}

template <typename Values>
void append_array(std::string &output, const Values &values, std::size_t offset,
                  std::size_t count) {
  output.push_back('[');
  for (std::size_t index = 0; index < count; ++index) {
    if (index != 0) {
      output.push_back(',');
    }
    append_number(output, values[offset + index]);
  }
  output.push_back(']');
}

std::string serialize(const RecordingRecord &record) {
  require_finite(record.elapsed_seconds);
  for (const double value : record.scaled) {
    require_finite(value);
  }
  std::string output{"{\"schema_version\":"};
  append_number(output, machine_schema_version);
  output += R"(,"timestamp_utc":")" + record.timestamp_utc + R"(","sample":{)";
  output += "\"elapsed_seconds\":";
  append_number(output, record.elapsed_seconds);
  output += ",\"rdt_sequence\":";
  append_number(output, record.rdt_sequence);
  output += ",\"ft_sequence\":";
  append_number(output, record.ft_sequence);
  output += ",\"configuration_revision\":" + std::to_string(record.configuration_revision);
  output += ",\"status\":";
  append_number(output, record.status);
  output += ",\"raw\":";
  append_array(output, record.raw, 0, record.raw.size());
  output += R"(,"force":{"raw":)";
  append_array(output, record.raw, 0, 3);
  output += ",\"value\":";
  append_array(output, record.scaled, 0, 3);
  output += R"(,"unit":")" + record.force_unit;
  output.push_back('"');
  output.push_back('}');
  output += R"(,"torque":{"raw":)";
  append_array(output, record.raw, 3, 3);
  output += ",\"value\":";
  append_array(output, record.scaled, 3, 3);
  output += R"(,"unit":")" + record.torque_unit;
  output.push_back('"');
  output += "}}}\n";
  return output;
}

} // namespace

void NdjsonRecordingWriter::write(const RecordingRecord &record) {
  const auto output = serialize(record);
  stream_.write(output.data(), static_cast<std::streamsize>(output.size()));
  if (!stream_) {
    throw AppError{ExitCode::Io, "failed to write recording"};
  }
}

} // namespace netft_cli
