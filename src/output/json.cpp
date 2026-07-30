#include "output/json.hpp"

#include "app/error.hpp"

#include <array>
#include <cmath>
#include <cstdint>
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

void require_finite(double value) {
  if (!std::isfinite(value)) {
    throw AppError{ExitCode::Io, "non-finite sample value"};
  }
}

void validate(const ConfigurationRecord &record) {
  require_finite(record.counts_per_force_unit);
  require_finite(record.counts_per_torque_unit);
}

void validate(const SampleRecord &record) {
  require_finite(record.elapsed_seconds);
  for (double value : record.scaled) {
    require_finite(value);
  }
  require_finite(record.receive_rate_hz);
}

void validate(const BiasRecord &record) {
  validate(record.configuration);
  validate(record.before);
  validate(record.after);
}

bool valid_utf8(std::string_view value) {
  std::size_t index = 0;
  while (index < value.size()) {
    const auto first = static_cast<unsigned char>(value[index]);
    if (first <= 0x7F) {
      ++index;
      continue;
    }

    std::size_t continuation_count{};
    std::uint32_t code_point{};
    if (first >= 0xC2 && first <= 0xDF) {
      continuation_count = 1;
      code_point = first & 0x1FU;
    } else if (first >= 0xE0 && first <= 0xEF) {
      continuation_count = 2;
      code_point = first & 0x0FU;
    } else if (first >= 0xF0 && first <= 0xF4) {
      continuation_count = 3;
      code_point = first & 0x07U;
    } else {
      return false;
    }

    if (index + continuation_count >= value.size()) {
      return false;
    }
    for (std::size_t offset = 1; offset <= continuation_count; ++offset) {
      const auto continuation = static_cast<unsigned char>(value[index + offset]);
      if ((continuation & 0xC0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (continuation & 0x3FU);
    }

    if ((continuation_count == 2 && code_point < 0x800U) ||
        (continuation_count == 3 && code_point < 0x10000U) ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU) || code_point > 0x10FFFFU) {
      return false;
    }
    index += continuation_count + 1;
  }
  return true;
}

void append_string(std::string &output, std::string_view value) {
  if (!valid_utf8(value)) {
    throw AppError{ExitCode::Io, "invalid UTF-8 output value"};
  }

  constexpr std::string_view hex{"0123456789abcdef"};
  output.push_back('"');
  for (const unsigned char character : value) {
    switch (character) {
    case '"':
      output += "\\\"";
      break;
    case '\\':
      output += "\\\\";
      break;
    case '\b':
      output += "\\b";
      break;
    case '\f':
      output += "\\f";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      if (character < 0x20U) {
        output += "\\u00";
        output.push_back(hex[character >> 4U]);
        output.push_back(hex[character & 0x0FU]);
      } else {
        output.push_back(static_cast<char>(character));
      }
    }
  }
  output.push_back('"');
}

template <typename Integer> void append_integer(std::string &output, Integer value) {
  static_assert(std::is_integral_v<Integer>);
  output += std::to_string(value);
}

void append_double(std::string &output, double value) {
  require_finite(value);
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
  output += stream.str();
}

template <typename Value, std::size_t Size, typename Append>
void append_array(std::string &output, const std::array<Value, Size> &values, std::size_t offset,
                  std::size_t count, Append append) {
  output.push_back('[');
  for (std::size_t index = 0; index < count; ++index) {
    if (index != 0) {
      output.push_back(',');
    }
    append(output, values[offset + index]);
  }
  output.push_back(']');
}

void append_name(std::string &output, std::string_view name, bool &first) {
  if (!first) {
    output.push_back(',');
  }
  first = false;
  append_string(output, name);
  output.push_back(':');
}

std::string serialize(const ConfigurationRecord &record) {
  validate(record);
  std::string output{"{"};
  bool first = true;
  append_name(output, "schema_version", first);
  append_integer(output, machine_schema_version);
  append_name(output, "host", first);
  append_string(output, record.host);
  append_name(output, "http_port", first);
  append_integer(output, record.http_port);
  append_name(output, "rdt_port", first);
  append_integer(output, record.rdt_port);
  append_name(output, "product_name", first);
  append_string(output, record.product_name);
  append_name(output, "calibration", first);
  output.push_back('{');
  bool first_calibration = true;
  append_name(output, "source", first_calibration);
  append_string(output, record.calibration_source);
  append_name(output, "revision", first_calibration);
  append_integer(output, record.configuration_revision);
  append_name(output, "force", first_calibration);
  output.push_back('{');
  bool first_force = true;
  append_name(output, "counts_per_unit", first_force);
  append_double(output, record.counts_per_force_unit);
  append_name(output, "unit", first_force);
  append_string(output, record.force_unit);
  output.push_back('}');
  append_name(output, "torque", first_calibration);
  output.push_back('{');
  bool first_torque = true;
  append_name(output, "counts_per_unit", first_torque);
  append_double(output, record.counts_per_torque_unit);
  append_name(output, "unit", first_torque);
  append_string(output, record.torque_unit);
  output += "}}}";
  return output;
}

std::string serialize(const SampleRecord &record) {
  validate(record);
  std::string output{"{"};
  bool first = true;
  append_name(output, "schema_version", first);
  append_integer(output, machine_schema_version);
  append_name(output, "host", first);
  append_string(output, record.host);
  append_name(output, "elapsed_seconds", first);
  append_double(output, record.elapsed_seconds);
  append_name(output, "rdt_sequence", first);
  append_integer(output, record.rdt_sequence);
  append_name(output, "ft_sequence", first);
  append_integer(output, record.ft_sequence);
  append_name(output, "status", first);
  append_integer(output, record.status);
  append_name(output, "raw", first);
  append_array(output, record.raw, 0, record.raw.size(), append_integer<std::int32_t>);
  append_name(output, "force", first);
  output.push_back('{');
  bool first_force = true;
  append_name(output, "raw", first_force);
  append_array(output, record.raw, 0, 3, append_integer<std::int32_t>);
  append_name(output, "value", first_force);
  append_array(output, record.scaled, 0, 3, append_double);
  append_name(output, "unit", first_force);
  append_string(output, record.force_unit);
  output.push_back('}');
  append_name(output, "torque", first);
  output.push_back('{');
  bool first_torque = true;
  append_name(output, "raw", first_torque);
  append_array(output, record.raw, 3, 3, append_integer<std::int32_t>);
  append_name(output, "value", first_torque);
  append_array(output, record.scaled, 3, 3, append_double);
  append_name(output, "unit", first_torque);
  append_string(output, record.torque_unit);
  output.push_back('}');
  append_name(output, "receive_rate_hz", first);
  append_double(output, record.receive_rate_hz);
  append_name(output, "lost_count", first);
  append_integer(output, record.lost_count);
  append_name(output, "duplicate_count", first);
  append_integer(output, record.duplicate_count);
  append_name(output, "out_of_order_count", first);
  append_integer(output, record.out_of_order_count);
  append_name(output, "state", first);
  append_string(output, record.state);
  output.push_back('}');
  return output;
}

std::string serialize(const BiasRecord &record) {
  validate(record);
  std::string output{"{\"schema_version\":"};
  append_integer(output, machine_schema_version);
  output += ",\"configuration\":";
  output += serialize(record.configuration);
  output += ",\"before\":";
  output += serialize(record.before);
  output += ",\"after\":";
  output += serialize(record.after);
  output.push_back('}');
  return output;
}

template <typename Record>
void write_serialized(std::ostream &stream, const Record &record, bool newline) {
  std::string output = serialize(record);
  if (newline) {
    output.push_back('\n');
  }
  stream.write(output.data(), static_cast<std::streamsize>(output.size()));
  if (!stream) {
    throw AppError{ExitCode::Io, "failed to write output"};
  }
}

} // namespace

void write_json(std::ostream &stream, const ConfigurationRecord &record) {
  write_serialized(stream, record, false);
}

void write_json(std::ostream &stream, const SampleRecord &record) {
  write_serialized(stream, record, false);
}

void write_json(std::ostream &stream, const BiasRecord &record) {
  write_serialized(stream, record, false);
}

void write_ndjson(std::ostream &stream, const ConfigurationRecord &record) {
  write_serialized(stream, record, true);
}

void write_ndjson(std::ostream &stream, const SampleRecord &record) {
  write_serialized(stream, record, true);
}

void write_ndjson(std::ostream &stream, const BiasRecord &record) {
  write_serialized(stream, record, true);
}

} // namespace netft_cli
