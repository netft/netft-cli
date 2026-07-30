#include "commands/check.hpp"

#include "app/error.hpp"
#include "diagnostics/criteria.hpp"
#include "output/json.hpp"
#include "output/terminal.hpp"
#include "stream/acquisition.hpp"
#include "stream/health_collector.hpp"

#include <chrono>
#include <exception>

namespace netft_cli {
namespace {

OutputFormat resolve_format(const CheckOptions &options, const OutputContext &output) {
  switch (options.format) {
  case OutputFormat::Automatic:
    return !options.output.has_value() && output.output_is_terminal ? OutputFormat::Text
                                                                    : OutputFormat::Json;
  case OutputFormat::Text:
  case OutputFormat::Json:
    return options.format;
  case OutputFormat::Table:
  case OutputFormat::Ndjson:
  case OutputFormat::Csv:
    throw AppError{ExitCode::Usage, "output format is not supported by this command"};
  }
  throw AppError{ExitCode::Usage, "output format is not supported by this command"};
}

void discover(const CheckOptions &options, SensorBackend &backend) {
  try {
    static_cast<void>(backend.discover(options.connection));
  } catch (const std::exception &) {
    throw AppError{ExitCode::Discovery, "sensor discovery failed"};
  }
}

void require_operational(const netft::HealthSnapshot &health) {
  if (health.fault_code == netft::FaultCode::None) {
    return;
  }
  if (health.fault_code == netft::FaultCode::SeriousStatus) {
    throw AppError{ExitCode::Sensor, "sensor reported a fault"};
  }
  throw AppError{ExitCode::Stream, "sensor stream faulted during check"};
}

} // namespace

int run_check(const CheckOptions &options, SensorBackend &backend, OutputContext &output,
              InterruptFlag &interrupt, Clock &clock) {
  const auto format = resolve_format(options, output);
  OutputHandle destination = options.output.has_value()
                                 ? OutputHandle::file(*options.output)
                                 : OutputHandle::standard(output.standard_output);
  discover(options, backend);
  auto acquisition = Acquisition::open(backend, options.connection);
  const auto baseline = acquisition.health();
  const auto start = clock.now();
  HealthCollector collector(start, baseline);
  acquisition.start([&collector](const netft::Sample &sample) { collector.observe(sample); });

  if (interrupt.requested()) {
    acquisition.stop();
    destination.flush();
    return static_cast<int>(ExitCode::Interrupted);
  }
  const auto duration =
      std::chrono::duration_cast<Clock::Duration>(std::chrono::duration<double>{options.duration});
  if (duration <= Clock::Duration::zero() || start > Clock::TimePoint::max() - duration) {
    throw AppError{ExitCode::Usage, "duration is outside the supported clock range"};
  }
  const auto deadline = start + duration;
  if (!clock.wait_until(deadline, interrupt)) {
    acquisition.stop();
    destination.flush();
    return static_cast<int>(ExitCode::Interrupted);
  }

  const auto final_health = acquisition.health();
  const auto observation = collector.finish(clock.now(), final_health);
  acquisition.stop();
  require_operational(final_health);
  const DiagnosticCriteria criteria{options.min_rate_hz, options.max_loss_percent,
                                    options.max_reconnects};
  const auto result = evaluate_diagnostics(true, observation, criteria);

  if (format == OutputFormat::Json) {
    write_json(destination.stream(), result);
  } else {
    destination.stream() << render_diagnostic_text(result);
  }
  destination.flush();
  return result.overall == DiagnosticOutcome::Fail ? static_cast<int>(ExitCode::Acceptance) : 0;
}

} // namespace netft_cli
