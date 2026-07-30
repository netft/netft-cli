#pragma once

#include "platform/clock.hpp"

#include <netft/types.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace netft_cli {

struct RecordingRecord {
  std::string timestamp_utc;
  double elapsed_seconds{};
  std::uint32_t rdt_sequence{}, ft_sequence{}, status{};
  std::array<std::int32_t, 6> raw{};
  std::array<double, 6> scaled{};
  std::string force_unit;
  std::string torque_unit;
};

[[nodiscard]] RecordingRecord make_recording_record(const netft::Sample &sample,
                                                    std::chrono::steady_clock::time_point origin,
                                                    WallClock::TimePoint timestamp);

class RecordingWriter {
public:
  virtual ~RecordingWriter() = default;
  virtual void write(const RecordingRecord &record) = 0;
};

} // namespace netft_cli
