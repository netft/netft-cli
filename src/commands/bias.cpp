#include "commands/bias.hpp"

#include "app/error.hpp"
#include "monitor/latest_sample.hpp"
#include "output/json.hpp"
#include "output/records.hpp"
#include "output/terminal.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>

namespace netft_cli {
namespace {

OutputFormat resolve_format(const BiasOptions &options, const OutputContext &output) {
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

netft::SensorConfiguration discover(const BiasOptions &options, SensorBackend &backend) {
  try {
    return backend.discover(options.connection);
  } catch (const std::exception &) {
    throw AppError{ExitCode::Discovery, "sensor discovery failed"};
  }
}

std::unique_ptr<SensorSession> open_session(const BiasOptions &options, SensorBackend &backend) {
  try {
    return backend.open(options.connection);
  } catch (const std::exception &) {
    throw AppError{ExitCode::Stream, "sensor stream could not be opened"};
  }
}

class SessionStop {
public:
  explicit SessionStop(SensorSession &session) noexcept : session_(session) {}
  ~SessionStop() { session_.stop(); }

  SessionStop(const SessionStop &) = delete;
  SessionStop &operator=(const SessionStop &) = delete;

private:
  SensorSession &session_;
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
  if (health.fault_code == netft::FaultCode::SeriousStatus) {
    throw AppError{ExitCode::Sensor, "sensor reported a fault"};
  }
  throw AppError{ExitCode::Stream, "sensor stream faulted"};
}

int interrupted_status() { return static_cast<int>(ExitCode::Interrupted); }

enum class BiasPhase { Before, Sending, After };

} // namespace

int run_bias(const BiasOptions &options, SensorBackend &backend, OutputContext &output,
             Confirmation &confirmation, InterruptFlag &interrupt) {
  const auto format = resolve_format(options, output);
  OutputHandle destination = options.output.has_value()
                                 ? OutputHandle::file(*options.output)
                                 : OutputHandle::standard(output.standard_output);
  if (!destination.stream()) {
    throw AppError{ExitCode::Io, "output stream is not writable"};
  }
  const auto configuration =
      make_configuration_record(options.connection, discover(options, backend));
  LatestSampleSlot before_slot;
  LatestSampleSlot after_slot;
  std::atomic<BiasPhase> phase{BiasPhase::Before};
  std::atomic<std::uint32_t> preview_sequence{};
  auto session = open_session(options, backend);
  SessionStop stop_session(*session);
  const auto origin = std::chrono::steady_clock::now();

  try {
    session->start([&](const netft::Sample &sample) {
      const auto current_phase = phase.load(std::memory_order_acquire);
      if (current_phase == BiasPhase::After &&
          sample.rdt_sequence != preview_sequence.load(std::memory_order_acquire)) {
        after_slot.publish(sample);
      } else if (current_phase == BiasPhase::Before) {
        before_slot.publish(sample);
      }
    });
  } catch (const std::exception &) {
    throw AppError{ExitCode::Stream, "sensor stream could not be started"};
  }

  if (interrupt.requested()) {
    return interrupted_status();
  }
  if (!before_slot.wait_for_first(options.connection.timeout, interrupt)) {
    if (interrupt.requested()) {
      return interrupted_status();
    }
    require_healthy(read_health(*session));
    throw AppError{ExitCode::Stream, "sensor stream produced no sample before timeout"};
  }

  const auto before_sample = before_slot.snapshot();
  if (!before_sample) {
    throw AppError{ExitCode::Stream, "sensor stream has no current sample"};
  }
  const auto before_health = read_health(*session);
  require_healthy(before_health);
  const BiasPreview preview{configuration,
                            make_sample_record(*before_sample, before_health, origin)};

  if (!options.assume_yes) {
    if (!output.input_is_terminal) {
      throw AppError{ExitCode::Usage, "confirmation requires terminal input or --yes"};
    }
    if (interrupt.requested()) {
      return interrupted_status();
    }
    const bool confirmed = confirmation.confirm(preview);
    if (interrupt.requested()) {
      return interrupted_status();
    }
    if (!confirmed) {
      throw AppError{ExitCode::Usage, "bias was not confirmed"};
    }
  }
  if (interrupt.requested()) {
    return interrupted_status();
  }

  preview_sequence.store(before_sample->rdt_sequence, std::memory_order_release);
  phase.store(BiasPhase::Sending, std::memory_order_release);
  try {
    session->bias([&phase] { phase.store(BiasPhase::After, std::memory_order_release); });
  } catch (const std::exception &) {
    throw AppError{ExitCode::Stream, "sensor bias command failed"};
  }
  require_healthy(read_health(*session));

  if (!after_slot.wait_for_first(options.connection.timeout, interrupt)) {
    if (interrupt.requested()) {
      return interrupted_status();
    }
    require_healthy(read_health(*session));
    throw AppError{ExitCode::Stream, "sensor stream produced no later sample before timeout"};
  }

  const auto after_sample = after_slot.snapshot();
  if (!after_sample) {
    throw AppError{ExitCode::Stream, "sensor stream has no post-bias sample"};
  }
  const auto after_health = read_health(*session);
  require_healthy(after_health);
  const BiasRecord result{preview.configuration, preview.sample,
                          make_sample_record(*after_sample, after_health, origin)};

  switch (format) {
  case OutputFormat::Text:
    destination.stream() << render_bias_text(result);
    break;
  case OutputFormat::Json:
    write_json(destination.stream(), result);
    break;
  case OutputFormat::Automatic:
  case OutputFormat::Table:
  case OutputFormat::Ndjson:
  case OutputFormat::Csv:
    throw AppError{ExitCode::Usage, "output format is not supported by this command"};
  }
  destination.flush();
  return 0;
}

} // namespace netft_cli
