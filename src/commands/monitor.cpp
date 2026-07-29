#include "commands/monitor.hpp"

#include "app/error.hpp"
#include "monitor/latest_sample.hpp"
#include "output/csv.hpp"
#include "output/json.hpp"
#include "output/records.hpp"
#include "output/terminal.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>

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

std::unique_ptr<SensorSession> open_session(const MonitorOptions &options, SensorBackend &backend) {
  try {
    return backend.open(options.connection);
  } catch (const std::exception &) {
    throw AppError{ExitCode::Stream, "sensor stream could not be opened"};
  }
}

class SessionStop {
public:
  explicit SessionStop(SensorSession &session) noexcept : session_(session) {}
  ~SessionStop() { stop(); }

  SessionStop(const SessionStop &) = delete;
  SessionStop &operator=(const SessionStop &) = delete;

  void stop() noexcept {
    if (active_) {
      session_.stop();
      active_ = false;
    }
  }

private:
  SensorSession &session_;
  bool active_{true};
};

netft::HealthSnapshot read_health(const SensorSession &session) {
  try {
    return session.health();
  } catch (const std::exception &) {
    throw AppError{ExitCode::Stream, "sensor stream health query failed"};
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

Clock::Duration period_for(double rate_hz) {
  const auto period =
      std::chrono::duration_cast<Clock::Duration>(std::chrono::duration<double>{1.0 / rate_hz});
  return std::max(period, Clock::Duration{1});
}

Clock::Duration duration_ticks(std::chrono::duration<double> duration) {
  return std::chrono::duration_cast<Clock::Duration>(duration);
}

} // namespace

int run_monitor(const MonitorOptions &options, SensorBackend &backend, OutputContext &output,
                InterruptFlag &interrupt, Clock &clock) {
  const auto format = resolve_format(options, output);
  OutputHandle destination = options.output.has_value()
                                 ? OutputHandle::file(*options.output)
                                 : OutputHandle::standard(output.standard_output);
  discover(options, backend);
  LatestSampleSlot latest;
  auto session = open_session(options, backend);
  SessionStop stop_session(*session);
  const auto origin = clock.now();

  try {
    session->start([&latest](const netft::Sample &sample) { latest.publish(sample); });
  } catch (const std::exception &) {
    throw AppError{ExitCode::Stream, "sensor stream could not be started"};
  }

  if (interrupt.requested()) {
    stop_session.stop();
    destination.flush();
    return static_cast<int>(ExitCode::Interrupted);
  }
  if (!latest.wait_for_first(options.connection.timeout, interrupt)) {
    if (interrupt.requested()) {
      stop_session.stop();
      destination.flush();
      return static_cast<int>(ExitCode::Interrupted);
    }
    require_healthy(read_health(*session));
    throw AppError{ExitCode::Stream, "sensor stream produced no sample before timeout"};
  }

  SampleOutput sample_output(format, destination,
                             !options.output.has_value() && output.output_is_terminal);
  const auto period = period_for(options.rate_hz);
  auto deadline = origin + period;
  const std::optional<Clock::TimePoint> end =
      options.duration ? std::optional<Clock::TimePoint>{origin + duration_ticks(*options.duration)}
                       : std::nullopt;
  const auto finish = [&](int status) {
    sample_output.close();
    stop_session.stop();
    destination.flush();
    return status;
  };

  for (;;) {
    if (interrupt.requested()) {
      return finish(static_cast<int>(ExitCode::Interrupted));
    }

    auto now = clock.now();
    while (deadline < now) {
      deadline += period;
    }
    if (end && deadline > *end) {
      return finish(0);
    }

    clock.sleep_until(deadline);
    if (interrupt.requested()) {
      return finish(static_cast<int>(ExitCode::Interrupted));
    }
    if (end && clock.now() > *end) {
      return finish(0);
    }

    const auto health = read_health(*session);
    require_healthy(health);
    const auto sample = latest.snapshot();
    if (!sample) {
      throw AppError{ExitCode::Stream, "sensor stream has no current sample"};
    }
    sample_output.write(make_sample_record(*sample, health, origin));
    deadline += period;
  }
}

} // namespace netft_cli
