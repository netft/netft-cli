#pragma once

#include "cli/options.hpp"
#include "platform/clock.hpp"
#include "platform/interrupt.hpp"
#include "recording/writer.hpp"
#include "sensor/backend.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>

namespace netft_cli {

struct RecorderLimits {
  std::optional<std::chrono::duration<double>> duration;
  std::optional<std::uint64_t> count;
  std::size_t queue_capacity{8192};
};

struct RecorderResult {
  std::uint64_t written_count{};
  bool interrupted{};
  std::uint64_t accepted_count{};
  double sample_span_seconds{};
  std::set<std::uint64_t> configuration_revisions;
  std::set<std::string> force_units, torque_units;
  std::uint64_t recorded_rdt_gaps{};
  std::uint64_t reconnect_count{};
};

class Recorder {
public:
  Recorder(SensorBackend &backend, ConnectionOptions connection, RecordingWriter &writer,
           Clock &clock, WallClock &wall_clock, InterruptFlag &interrupt);

  [[nodiscard]] RecorderResult run(const RecorderLimits &limits,
                                   const std::function<void()> &finalize);

private:
  SensorBackend &backend_;
  ConnectionOptions connection_;
  RecordingWriter &writer_;
  Clock &clock_;
  WallClock &wall_clock_;
  InterruptFlag &interrupt_;
};

} // namespace netft_cli
