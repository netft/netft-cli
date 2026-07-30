#include "output/csv.hpp"

#include "app/error.hpp"

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

constexpr std::string_view header = "host,schema_version,elapsed_seconds,rdt_sequence,"
                                    "ft_sequence,status,"
                                    "raw_fx,raw_fy,raw_fz,raw_tx,raw_ty,raw_tz,"
                                    "fx,fy,fz,tx,ty,tz,force_unit,torque_unit,receive_rate_hz,"
                                    "lost_count,duplicate_count,out_of_order_count,state\r\n";

void require_finite(double value) {
  if (!std::isfinite(value)) {
    throw AppError{ExitCode::Io, "non-finite sample value"};
  }
}

void validate(const SampleRecord &record) {
  require_finite(record.elapsed_seconds);
  for (double value : record.scaled) {
    require_finite(value);
  }
  require_finite(record.receive_rate_hz);
}

void append_field(std::string &output, std::string_view field) {
  if (field.find_first_of(",\"\r\n") == std::string_view::npos) {
    output.append(field);
    return;
  }

  output.push_back('"');
  for (char character : field) {
    if (character == '"') {
      output += "\"\"";
    } else {
      output.push_back(character);
    }
  }
  output.push_back('"');
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

void append_separator(std::string &output) { output.push_back(','); }

template <typename Value> void append_number(std::string &output, Value value) {
  output += number(value);
}

std::string serialize_row(const SampleRecord &record) {
  validate(record);
  std::string output;
  append_field(output, record.host);
  append_separator(output);
  append_number(output, machine_schema_version);
  append_separator(output);
  append_number(output, record.elapsed_seconds);
  append_separator(output);
  append_number(output, record.rdt_sequence);
  append_separator(output);
  append_number(output, record.ft_sequence);
  append_separator(output);
  append_number(output, record.status);
  for (const auto value : record.raw) {
    append_separator(output);
    append_number(output, value);
  }
  for (const auto value : record.scaled) {
    append_separator(output);
    append_number(output, value);
  }
  append_separator(output);
  append_field(output, record.force_unit);
  append_separator(output);
  append_field(output, record.torque_unit);
  append_separator(output);
  append_number(output, record.receive_rate_hz);
  append_separator(output);
  append_number(output, record.lost_count);
  append_separator(output);
  append_number(output, record.duplicate_count);
  append_separator(output);
  append_number(output, record.out_of_order_count);
  append_separator(output);
  append_field(output, record.state);
  output += "\r\n";
  return output;
}

} // namespace

void CsvWriter::write(const SampleRecord &record) {
  std::string output;
  if (!header_written_) {
    output.append(header);
  }
  output += serialize_row(record);
  stream_.write(output.data(), static_cast<std::streamsize>(output.size()));
  if (!stream_) {
    throw AppError{ExitCode::Io, "failed to write output"};
  }
  header_written_ = true;
}

} // namespace netft_cli
