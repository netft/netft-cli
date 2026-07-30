#include "recording/csv_writer.hpp"

#include "app/error.hpp"
#include "output/records.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace netft_cli {
namespace {

constexpr std::string_view header =
    "schema_version,timestamp_utc,elapsed_seconds,rdt_sequence,ft_sequence,status,"
    "raw_fx,raw_fy,raw_fz,raw_tx,raw_ty,raw_tz,"
    "fx,fy,fz,tx,ty,tz,force_unit,torque_unit\r\n";

void require_finite(double value) {
  if (!std::isfinite(value)) {
    throw AppError{ExitCode::Io, "non-finite recording value"};
  }
}

template <typename Value> std::string number(Value value) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  if constexpr (std::is_floating_point_v<Value>) {
    stream << std::setprecision(std::numeric_limits<Value>::max_digits10);
  }
  stream << value;
  return stream.str();
}

void append_field(std::string &output, std::string_view field) {
  if (field.find_first_of(",\"\r\n") == std::string_view::npos) {
    output.append(field);
    return;
  }
  output.push_back('"');
  for (const char character : field) {
    if (character == '"') {
      output += "\"\"";
    } else {
      output.push_back(character);
    }
  }
  output.push_back('"');
}

template <typename Value> void append_value(std::string &output, Value value) {
  output.push_back(',');
  output += number(value);
}

std::string serialize(const RecordingRecord &record) {
  require_finite(record.elapsed_seconds);
  for (const double value : record.scaled) {
    require_finite(value);
  }
  std::string output = number(machine_schema_version);
  output.push_back(',');
  append_field(output, record.timestamp_utc);
  append_value(output, record.elapsed_seconds);
  append_value(output, record.rdt_sequence);
  append_value(output, record.ft_sequence);
  append_value(output, record.status);
  for (const auto value : record.raw) {
    append_value(output, value);
  }
  for (const auto value : record.scaled) {
    append_value(output, value);
  }
  output.push_back(',');
  append_field(output, record.force_unit);
  output.push_back(',');
  append_field(output, record.torque_unit);
  output += "\r\n";
  return output;
}

} // namespace

void CsvRecordingWriter::write(const RecordingRecord &record) {
  std::string output;
  if (!header_written_) {
    output.append(header);
  }
  output += serialize(record);
  stream_.write(output.data(), static_cast<std::streamsize>(output.size()));
  if (!stream_) {
    throw AppError{ExitCode::Io, "failed to write recording"};
  }
  header_written_ = true;
}

} // namespace netft_cli
