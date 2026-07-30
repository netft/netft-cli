#include "output/terminal.hpp"

#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace netft_cli {
namespace {

constexpr std::size_t label_width = 10;
constexpr std::size_t numeric_width = 14;
constexpr std::size_t frame_width = label_width + (6 * numeric_width);
constexpr std::size_t frame_height = 10;
constexpr std::size_t host_width = 72;
constexpr std::size_t state_width = 48;
constexpr std::size_t rate_width = 14;
constexpr std::size_t elapsed_width = 20;

std::string sanitize_human_text(std::string_view text) {
  std::string sanitized;
  sanitized.reserve(text.size());
  for (const char character : text) {
    const auto byte = static_cast<unsigned char>(character);
    sanitized.push_back(byte < 0x20U || byte == 0x7FU ? '?' : character);
  }
  return sanitized;
}

std::string fixed_field(std::string_view value, std::size_t width) {
  const auto sanitized = sanitize_human_text(value);
  if (sanitized.size() > width) {
    std::string overflow(width, '#');
    return overflow;
  }
  return std::string(width - sanitized.size(), ' ') + sanitized;
}

std::string bounded_text(std::string_view value, std::size_t width) {
  const auto sanitized = sanitize_human_text(value);
  if (sanitized.size() > width) {
    std::string overflow(width, '#');
    return overflow;
  }
  return sanitized + std::string(width - sanitized.size(), ' ');
}

std::string fixed_integer(std::int32_t value) {
  return fixed_field(std::to_string(value), numeric_width);
}

std::string fixed_decimal(double value) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(5) << value;
  return fixed_field(stream.str(), numeric_width);
}

std::string decimal(double value, int precision) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(precision) << value;
  return stream.str();
}

std::string pad_frame_rows(std::string_view frame) {
  std::string padded;
  padded.reserve((frame_width + 1) * frame_height);
  std::size_t start = 0;
  while (start < frame.size()) {
    const auto end = frame.find('\n', start);
    const auto row =
        frame.substr(start, end == std::string_view::npos ? frame.size() - start : end - start);
    if (row.size() > frame_width) {
      throw std::logic_error("terminal frame row exceeds fixed width");
    }
    padded.append(row);
    padded.append(frame_width - row.size(), ' ');
    padded.push_back('\n');
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return padded;
}

template <typename Values, typename Formatter>
void append_measurement_row(std::ostringstream &stream, std::string_view label,
                            const Values &values, Formatter formatter) {
  stream << std::left << std::setw(static_cast<int>(label_width)) << label << std::right;
  for (const auto value : values) {
    stream << formatter(value);
  }
  stream << '\n';
}

std::string render_frame(const SampleRecord &record) {
  std::ostringstream stream;
  stream << "NetFT monitor  host=" << bounded_text(record.host, host_width) << '\n';
  stream << "Connection   state=" << bounded_text(record.state, state_width)
         << "  rate_hz=" << fixed_field(decimal(record.receive_rate_hz, 2), rate_width) << '\n';
  stream << "Sequences  rdt=" << record.rdt_sequence << "  ft=" << record.ft_sequence
         << "  status=" << record.status << '\n';
  stream << "Health     lost=" << record.lost_count << "  duplicate=" << record.duplicate_count
         << '\n';
  stream << "Ordering   out_of_order=" << record.out_of_order_count << '\n';

  const std::array<std::string_view, 6> axes{"Fx", "Fy", "Fz", "Tx", "Ty", "Tz"};
  stream << std::left << std::setw(static_cast<int>(label_width)) << "Axis" << std::right;
  for (const auto axis : axes) {
    stream << fixed_field(axis, numeric_width);
  }
  stream << '\n';

  append_measurement_row(stream, "Raw", record.raw,
                         [](std::int32_t value) { return fixed_integer(value); });
  append_measurement_row(stream, "Converted", record.scaled,
                         [](double value) { return fixed_decimal(value); });

  const std::array<std::string, 6> units{record.force_unit,  record.force_unit,
                                         record.force_unit,  record.torque_unit,
                                         record.torque_unit, record.torque_unit};
  stream << std::left << std::setw(static_cast<int>(label_width)) << "Units" << std::right;
  for (const auto &unit : units) {
    stream << fixed_field(unit, numeric_width);
  }
  stream << '\n';
  stream << "Elapsed    " << fixed_field(decimal(record.elapsed_seconds, 5), elapsed_width)
         << " s\n";
  return pad_frame_rows(stream.str());
}

std::string render_compact_line(const SampleRecord &record) {
  std::ostringstream stream;
  stream << "host=" << sanitize_human_text(record.host)
         << " state=" << sanitize_human_text(record.state)
         << " rate_hz=" << decimal(record.receive_rate_hz, 2) << " rdt=" << record.rdt_sequence
         << " ft=" << record.ft_sequence << " status=" << record.status
         << " lost=" << record.lost_count << " duplicate=" << record.duplicate_count
         << " out_of_order=" << record.out_of_order_count << " raw=[";
  for (std::size_t index = 0; index < record.raw.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    stream << record.raw[index];
  }
  stream << "] converted=[";
  for (std::size_t index = 0; index < record.scaled.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    stream << decimal(record.scaled[index], 5);
  }
  stream << "] units=[" << sanitize_human_text(record.force_unit) << ','
         << sanitize_human_text(record.torque_unit) << "]\n";
  return stream.str();
}

void append_sample_text(std::ostringstream &stream, std::string_view label,
                        const SampleRecord &record) {
  stream << label << " sequence: " << record.rdt_sequence << '\n';
  append_measurement_row(stream, "Raw", record.raw,
                         [](std::int32_t value) { return fixed_integer(value); });
  append_measurement_row(stream, "Converted", record.scaled,
                         [](double value) { return fixed_decimal(value); });
  stream << "Units: force=" << sanitize_human_text(record.force_unit)
         << " torque=" << sanitize_human_text(record.torque_unit) << '\n';
}

bool supports_frame(TerminalCapabilities capabilities) {
  return capabilities.ansi && capabilities.width >= frame_width &&
         capabilities.height >= frame_height;
}

} // namespace

std::string render_configuration_text(const ConfigurationRecord &record) {
  std::ostringstream stream;
  stream << "Sensor: " << sanitize_human_text(record.product_name) << '\n';
  stream << "Endpoint: " << sanitize_human_text(record.host) << " HTTP " << record.http_port
         << " RDT " << record.rdt_port << '\n';
  stream << "Force calibration: " << decimal(record.counts_per_force_unit, 5) << " counts/"
         << sanitize_human_text(record.force_unit) << '\n';
  stream << "Torque calibration: " << decimal(record.counts_per_torque_unit, 5) << " counts/"
         << sanitize_human_text(record.torque_unit) << '\n';
  stream << "Calibration source: " << sanitize_human_text(record.calibration_source)
         << " revision=" << record.configuration_revision << '\n';
  return stream.str();
}

std::string render_bias_text(const BiasRecord &record) {
  std::ostringstream stream;
  stream << render_configuration_text(record.configuration);
  append_sample_text(stream, "Before", record.before);
  append_sample_text(stream, "After", record.after);
  return stream.str();
}

std::string render_diagnostic_text(const DiagnosticResult &result) {
  const char *const outcome = [&] {
    switch (result.overall) {
    case DiagnosticOutcome::Pass:
      return "PASS";
    case DiagnosticOutcome::PassWithWarnings:
      return "PASS WITH WARNINGS";
    case DiagnosticOutcome::Fail:
      return "FAIL";
    }
    return "FAIL";
  }();
  std::ostringstream stream;
  stream << "NetFT check: " << outcome << '\n';
  stream << "Samples: " << result.health.sample_count
         << "  rate_hz: " << decimal(result.health.observed_rate_hz, 2)
         << "  lost: " << result.health.lost_count
         << "  reconnects: " << result.health.reconnect_count << '\n';
  for (const auto &check : result.checks) {
    stream << "- " << diagnostic_name(check.id) << ": " << diagnostic_status_name(check.status);
    if (check.has_limit) {
      stream << " (observed=" << decimal(check.observed, 3) << ", limit=" << decimal(check.limit, 3)
             << ')';
    }
    stream << '\n';
  }
  return stream.str();
}

TerminalMonitor::TerminalMonitor(TerminalWriter &terminal) noexcept : terminal_(terminal) {}

TerminalMonitor::~TerminalMonitor() {
  try {
    close();
  } catch (...) {
    static_cast<void>(0);
  }
}

void TerminalMonitor::render(const SampleRecord &record) {
  if (closed_) {
    throw std::logic_error("cannot render after closing terminal monitor");
  }

  if (supports_frame(terminal_.capabilities())) {
    if (!frame_started_) {
      terminal_.clear_frame();
      frame_started_ = true;
    }
    terminal_.home();
    terminal_.write(render_frame(record));
  } else {
    terminal_.write(render_compact_line(record));
  }
  terminal_.flush();
}

void TerminalMonitor::close() {
  if (closed_) {
    return;
  }
  closed_ = true;
  if (frame_started_) {
    terminal_.write("\n");
    terminal_.flush();
  }
}

} // namespace netft_cli
