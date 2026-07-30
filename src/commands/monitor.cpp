#include "commands/monitor.hpp"

#include "app/error.hpp"
#include "output/csv.hpp"
#include "output/json.hpp"
#include "output/records.hpp"
#include "output/terminal.hpp"
#include "stream/acquisition.hpp"
#include "stream/latest_sample.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace netft_cli {
namespace {

OutputFormat resolve_format(const MonitorOptions &options, const OutputContext &output) {
  switch (options.format) {
  case OutputFormat::Automatic:
    return !options.output.has_value() && output.output_is_terminal ? OutputFormat::Table
                                                                    : OutputFormat::Ndjson;
  case OutputFormat::Table:
  case OutputFormat::Ndjson:
  case OutputFormat::Csv:
    return options.format;
  case OutputFormat::Text:
  case OutputFormat::Json:
    throw AppError{ExitCode::Usage, "output format is not supported by this command"};
  }
  throw AppError{ExitCode::Usage, "output format is not supported by this command"};
}

void discover(const MonitorOptions &options, SensorBackend &backend) {
  try {
    static_cast<void>(backend.discover(options.connection));
  } catch (const std::exception &) {
    throw AppError{ExitCode::Discovery, "sensor discovery failed"};
  }
}

void require_healthy(const netft::HealthSnapshot &health) {
  if (health.state != netft::ClientState::Faulted && health.fault_code == netft::FaultCode::None) {
    return;
  }
  const auto code =
      health.fault_code == netft::FaultCode::SeriousStatus ? ExitCode::Sensor : ExitCode::Stream;
  throw AppError{code,
                 code == ExitCode::Sensor ? "sensor reported a fault" : "sensor stream faulted"};
}

class SampleOutput {
public:
  SampleOutput(OutputFormat format, OutputHandle &destination, bool output_is_terminal)
      : format_(format), destination_(destination) {
    if (format_ == OutputFormat::Csv) {
      csv_ = std::make_unique<CsvWriter>(destination_.stream());
    } else if (format_ == OutputFormat::Table) {
      const int descriptor = output_is_terminal ? 1 : -1;
      terminal_writer_ = make_terminal_writer(destination_.stream(), descriptor);
      terminal_monitor_ = std::make_unique<TerminalMonitor>(*terminal_writer_);
    }
  }

  void write(const SampleRecord &record) {
    switch (format_) {
    case OutputFormat::Table:
      terminal_monitor_->render(record);
      break;
    case OutputFormat::Ndjson:
      write_ndjson(destination_.stream(), record);
      break;
    case OutputFormat::Csv:
      csv_->write(record);
      break;
    case OutputFormat::Automatic:
    case OutputFormat::Text:
    case OutputFormat::Json:
      throw AppError{ExitCode::Usage, "output format is not supported by this command"};
    }
    destination_.flush();
  }

  void close() {
    if (terminal_monitor_) {
      terminal_monitor_->close();
    }
  }

private:
  OutputFormat format_;
  OutputHandle &destination_;
  std::unique_ptr<CsvWriter> csv_;
  std::unique_ptr<TerminalWriter> terminal_writer_;
  std::unique_ptr<TerminalMonitor> terminal_monitor_;
};

Clock::Duration checked_clock_duration(std::chrono::duration<double> value, std::string_view name) {
  const double seconds = value.count();
  const double minimum = std::chrono::duration<double>{Clock::Duration{1}}.count();
  const double maximum = std::chrono::duration<double>{Clock::Duration::max()}.count();
  if (!std::isfinite(seconds) || seconds < minimum || seconds >= maximum) {
    throw AppError{ExitCode::Usage, std::string{name} + " is outside the supported clock range"};
  }
  return std::chrono::duration_cast<Clock::Duration>(value);
}

Clock::Duration period_for(double rate_hz) {
  return checked_clock_duration(std::chrono::duration<double>{1.0 / rate_hz}, "rate");
}

Clock::Duration duration_ticks(std::chrono::duration<double> duration) {
  return checked_clock_duration(duration, "duration");
}

using Tick = Clock::Duration::rep;
using UnsignedTick = std::make_unsigned_t<Tick>;

static_assert(std::is_integral_v<Tick> && std::is_signed_v<Tick>);

UnsignedTick negative_magnitude(Tick value) {
  return static_cast<UnsignedTick>(-(value + 1)) + UnsignedTick{1};
}

UnsignedTick positive_distance(Tick later, Tick earlier) {
  if (earlier < 0 && later >= 0) {
    return negative_magnitude(earlier) + static_cast<UnsignedTick>(later);
  }
  return static_cast<UnsignedTick>(later - earlier);
}

Tick add_positive_ticks(Tick origin, UnsignedTick increment) {
  if (increment == 0) {
    return origin;
  }
  if (origin < 0) {
    const auto magnitude = negative_magnitude(origin);
    if (increment < magnitude) {
      return -static_cast<Tick>(magnitude - increment);
    }
    if (increment == magnitude) {
      return Tick{0};
    }
    return static_cast<Tick>(increment - magnitude);
  }
  return origin + static_cast<Tick>(increment);
}

std::optional<Clock::TimePoint> try_add(Clock::TimePoint origin, Clock::Duration duration) {
  if (duration <= Clock::Duration::zero()) {
    return std::nullopt;
  }
  const Tick origin_ticks = origin.time_since_epoch().count();
  const auto increment = static_cast<UnsignedTick>(duration.count());
  const Tick maximum = Clock::TimePoint::max().time_since_epoch().count();
  if (increment > positive_distance(maximum, origin_ticks)) {
    return std::nullopt;
  }
  return Clock::TimePoint{Clock::Duration{add_positive_ticks(origin_ticks, increment)}};
}

Clock::TimePoint checked_add(Clock::TimePoint origin, Clock::Duration duration,
                             std::string_view name) {
  if (const auto result = try_add(origin, duration)) {
    return *result;
  }
  throw AppError{ExitCode::Usage, std::string{name} + " is outside the supported clock range"};
}

Clock::TimePoint first_deadline_after(Clock::TimePoint origin, Clock::Duration period,
                                      Clock::TimePoint time) {
  if (time < origin) {
    return checked_add(origin, period, "rate");
  }
  const Tick origin_ticks = origin.time_since_epoch().count();
  const Tick time_ticks = time.time_since_epoch().count();
  const Tick maximum = Clock::TimePoint::max().time_since_epoch().count();
  const auto period_ticks = static_cast<UnsignedTick>(period.count());
  const auto completed_periods = positive_distance(time_ticks, origin_ticks) / period_ticks;
  const auto maximum_periods = positive_distance(maximum, origin_ticks) / period_ticks;
  if (completed_periods >= maximum_periods) {
    return Clock::TimePoint::max();
  }
  const auto increment = period_ticks * (completed_periods + UnsignedTick{1});
  return Clock::TimePoint{Clock::Duration{add_positive_ticks(origin_ticks, increment)}};
}

} // namespace

int run_monitor(const MonitorOptions &options, SensorBackend &backend, OutputContext &output,
                InterruptFlag &interrupt, Clock &clock) {
  const auto format = resolve_format(options, output);
  const auto record_origin = clock.now();
  const auto period = period_for(options.rate_hz);
  const auto duration = options.duration
                            ? std::optional<Clock::Duration>{duration_ticks(*options.duration)}
                            : std::nullopt;
  static_cast<void>(checked_add(record_origin, period, "rate"));
  if (duration) {
    static_cast<void>(checked_add(record_origin, *duration, "duration"));
  }
  OutputHandle destination = options.output.has_value()
                                 ? OutputHandle::file(*options.output)
                                 : OutputHandle::standard(output.standard_output);
  discover(options, backend);
  LatestSampleSlot latest;
  auto acquisition = Acquisition::open(backend, options.connection);
  acquisition.start([&latest](const netft::Sample &sample) { latest.publish(sample); });

  if (interrupt.requested()) {
    acquisition.stop();
    destination.flush();
    return static_cast<int>(ExitCode::Interrupted);
  }
  if (!latest.wait_for_first(options.connection.timeout, interrupt)) {
    if (interrupt.requested()) {
      acquisition.stop();
      destination.flush();
      return static_cast<int>(ExitCode::Interrupted);
    }
    require_healthy(acquisition.health());
    throw AppError{ExitCode::Stream, "sensor stream produced no sample before timeout"};
  }

  const auto schedule_origin = clock.now();
  const auto first_deadline = checked_add(schedule_origin, period, "rate");
  const auto end =
      duration
          ? std::optional<Clock::TimePoint>{checked_add(schedule_origin, *duration, "duration")}
          : std::nullopt;
  SampleOutput sample_output(format, destination,
                             !options.output.has_value() && output.output_is_terminal);
  auto deadline = first_deadline;
  const auto finish = [&](int status) {
    sample_output.close();
    acquisition.stop();
    destination.flush();
    return status;
  };

  for (;;) {
    if (interrupt.requested()) {
      return finish(static_cast<int>(ExitCode::Interrupted));
    }

    auto now = clock.now();
    if (deadline < now) {
      deadline = first_deadline_after(schedule_origin, period, now);
    }
    if (end && deadline > *end) {
      return finish(0);
    }

    if (!clock.wait_until(deadline, interrupt)) {
      return finish(static_cast<int>(ExitCode::Interrupted));
    }
    if (end && clock.now() > *end) {
      return finish(0);
    }

    const auto health = acquisition.health();
    require_healthy(health);
    const auto sample = latest.snapshot();
    if (!sample) {
      throw AppError{ExitCode::Stream, "sensor stream has no current sample"};
    }
    sample_output.write(make_sample_record(*sample, health, record_origin));
    deadline = first_deadline_after(schedule_origin, period, clock.now());
  }
}

} // namespace netft_cli
