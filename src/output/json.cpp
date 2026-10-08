#include "output/json.hpp"

#include "app/error.hpp"
#include "cli/schema.hpp"
#include "recording/recorder.hpp"
#include <set>

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

std::string serialize(const DiagnosticResult &result) {
  std::string output{"{"};
  bool first = true;
  append_name(output, "schema_version", first);
  append_integer(output, machine_schema_version);
  append_name(output, "result", first);
  append_string(output, diagnostic_outcome_name(result.overall));
  append_name(output, "metrics", first);
  output.push_back('{');
  bool first_metric = true;
  append_name(output, "elapsed_seconds", first_metric);
  append_double(output, result.health.elapsed_seconds);
  append_name(output, "sample_count", first_metric);
  append_integer(output, result.health.sample_count);
  append_name(output, "received_count", first_metric);
  append_integer(output, result.health.received_count);
  append_name(output, "observed_rate_hz", first_metric);
  append_double(output, result.health.observed_rate_hz);
  append_name(output, "lost_count", first_metric);
  append_integer(output, result.health.lost_count);
  append_name(output, "duplicate_count", first_metric);
  append_integer(output, result.health.duplicate_count);
  append_name(output, "out_of_order_count", first_metric);
  append_integer(output, result.health.out_of_order_count);
  append_name(output, "reconnect_count", first_metric);
  append_integer(output, result.health.reconnect_count);
  append_name(output, "nonzero_status_count", first_metric);
  append_integer(output, result.health.nonzero_status_count);
  output.push_back('}');
  append_name(output, "checks", first);
  output.push_back('[');
  for (std::size_t index = 0; index < result.checks.size(); ++index) {
    if (index != 0) {
      output.push_back(',');
    }
    const auto &check = result.checks[index];
    output.push_back('{');
    bool first_check = true;
    append_name(output, "name", first_check);
    append_string(output, diagnostic_name(check.id));
    append_name(output, "status", first_check);
    append_string(output, diagnostic_status_name(check.status));
    append_name(output, "observed", first_check);
    append_double(output, check.observed);
    if (check.has_limit) {
      append_name(output, "limit", first_check);
      append_double(output, check.limit);
    }
    output.push_back('}');
  }
  output += "]}";
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

void write_recording_metadata(std::ostream &stream, const RecorderResult &result) {
  std::string output{
      R"({"schema_version":1,"kind":"netft-recording","producer":"netft-cli","result":)"};
  append_string(output, result.interrupted ? "interrupted" : "complete");
  output += ",\"accepted_samples\":";
  append_integer(output, result.accepted_count);
  output += ",\"written_samples\":";
  append_integer(output, result.written_count);
  output += ",\"sample_span_seconds\":";
  append_double(output, result.sample_span_seconds);
  output += ",\"recorded_rdt_gaps\":";
  append_integer(output, result.recorded_rdt_gaps);
  output += ",\"reconnect_count\":";
  append_integer(output, result.reconnect_count);
  output += R"(,"error":null,"pause_count":0,"configuration_revisions":[)";
  bool first = true;
  for (const auto revision : result.configuration_revisions) {
    if (!first) {
      output.push_back(',');
    }
    first = false;
    append_integer(output, revision);
  }
  output += "],\"force_units\":[";
  first = true;
  for (const auto &unit : result.force_units) {
    if (!first) {
      output.push_back(',');
    }
    first = false;
    append_string(output, unit);
  }
  output += "],\"torque_units\":[";
  first = true;
  for (const auto &unit : result.torque_units) {
    if (!first) {
      output.push_back(',');
    }
    first = false;
    append_string(output, unit);
  }
  output += "]}\n";
  stream.write(output.data(), static_cast<std::streamsize>(output.size()));
  if (!stream) {
    throw AppError{ExitCode::Io, "failed to write recording metadata"};
  }
}

void write_command_schema(std::ostream &stream, const CommandSchema &schema,
                          std::string_view version, std::string_view source_commit,
                          bool source_dirty) {
  constexpr std::array<std::string_view, 10> types{"flag",
                                                   "host",
                                                   "port",
                                                   "positive-number",
                                                   "nonnegative-number",
                                                   "positive-integer",
                                                   "nonnegative-integer",
                                                   "duration",
                                                   "path",
                                                   "choice"};
  std::string output{R"({"schemaVersion":1,"kind":"cli","component":"netft-cli")"};
  auto string_field = [&](std::string_view key, std::string_view value) {
    output.push_back(',');
    append_string(output, key);
    output.push_back(':');
    append_string(output, value);
  };
  auto array = [&](const auto &values, auto append) {
    output.push_back('[');
    bool first = true;
    for (const auto &value : values) {
      if (!first) {
        output.push_back(',');
      }
      first = false;
      append(value);
    }
    output.push_back(']');
  };
  string_field("version", version);
  string_field("sourceCommit", source_commit);
  output += source_dirty ? ",\"sourceDirty\":true" : ",\"sourceDirty\":false";
  output += ",\"options\":";
  array(schema.options, [&](const OptionSpec &option) {
    output += "{\"id\":";
    append_string(output, option.long_name);
    string_field("longName", option.long_name);
    if (option.short_name) {
      string_field("shortName", std::string(1, option.short_name));
    }
    string_field("valueType", types.at(static_cast<std::size_t>(option.value_type)));
    string_field("description", option.description);
    output += option.repeatable ? ",\"repeatable\":true" : ",\"repeatable\":false";
    output += ",\"values\":";
    array(option.values, [&](auto value) { append_string(output, value); });
    output.push_back('}');
  });
  output += ",\"commands\":";
  std::set<int> exits;
  array(schema.commands, [&](const CommandSpec &command) {
    output += "{\"id\":";
    append_string(output, command.name);
    string_field("name", command.name);
    string_field("synopsis", command.usage);
    string_field("description", command.description);
    output += ",\"optionIds\":";
    array(command.options, [&](auto id) { append_string(output, schema.option(id).long_name); });
    output += ",\"positionals\":";
    array(command.positionals, [&](auto id) {
      const auto &position = schema.positional(id);
      output += "{\"name\":";
      append_string(output, position.name);
      string_field("valueType", types.at(static_cast<std::size_t>(position.value_type)));
      output += position.required ? ",\"required\":true" : ",\"required\":false";
      output += ",\"values\":";
      array(position.values, [&](auto value) { append_string(output, value); });
      output.push_back('}');
    });
    output += ",\"examples\":";
    array(command.examples, [&](auto value) { append_string(output, value); });
    output += ",\"exitStatuses\":";
    array(command.exit_statuses, [&](int value) {
      exits.insert(value);
      append_integer(output, value);
    });
    output.push_back('}');
  });
  output += ",\"exitStatuses\":";
  array(exits, [&](int code) {
    output += "{\"code\":";
    append_integer(output, code);
    const auto meaning = [](int exit_code) -> std::string_view {
      switch (exit_code) {
      case 0:
        return "Completed successfully";
      case 2:
        return "Invalid command line or configuration";
      case 3:
        return "Sensor discovery failed";
      case 4:
        return "Stream connection or acquisition failed";
      case 5:
        return "Sensor reported a fault";
      case 6:
        return "Input/output operation failed";
      case 7:
        return "A check acceptance criterion failed";
      case 8:
        return "Recording integrity could not be guaranteed";
      default:
        return "Interrupted";
      }
    }(code);
    string_field("meaning", meaning);
    output.push_back('}');
  });
  output += "}\n";
  stream.write(output.data(), static_cast<std::streamsize>(output.size()));
  if (!stream) {
    throw AppError{ExitCode::Io, "failed to write interface schema"};
  }
}

void write_json(std::ostream &stream, const ConfigurationRecord &record) {
  write_serialized(stream, record, false);
}

void write_json(std::ostream &stream, const SampleRecord &record) {
  write_serialized(stream, record, false);
}

void write_json(std::ostream &stream, const BiasRecord &record) {
  write_serialized(stream, record, false);
}

void write_json(std::ostream &stream, const DiagnosticResult &result) {
  write_serialized(stream, result, false);
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
