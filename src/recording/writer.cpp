#include "recording/writer.hpp"

#include <netft/types.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <string>

namespace netft_cli {
namespace {

std::string format_utc(WallClock::TimePoint timestamp) {
  const auto seconds = std::chrono::floor<std::chrono::seconds>(timestamp);
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(timestamp - seconds).count();
  const std::time_t value = WallClock::TimePoint::clock::to_time_t(seconds);
  std::tm utc{};
#ifdef _WIN32
  if (::gmtime_s(&utc, &value) != 0) {
#else
  if (::gmtime_r(&value, &utc) == nullptr) {
#endif
    throw std::runtime_error("UTC timestamp conversion failed");
  }
  std::array<char, 32> buffer{};
  const int length =
      std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
                    utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min,
                    utc.tm_sec, static_cast<long long>(milliseconds));
  if (length <= 0 || static_cast<std::size_t>(length) >= buffer.size()) {
    throw std::runtime_error("UTC timestamp formatting failed");
  }
  return {buffer.data(), static_cast<std::size_t>(length)};
}

} // namespace

RecordingRecord make_recording_record(const netft::Sample &sample,
                                      std::chrono::steady_clock::time_point origin,
                                      WallClock::TimePoint timestamp) {
  return {
      format_utc(timestamp),
      std::chrono::duration<double>{sample.received_at - origin}.count(),
      sample.rdt_sequence,
      sample.ft_sequence,
      sample.status,
      sample.raw_wrench,
      {sample.force[0], sample.force[1], sample.force[2], sample.torque[0], sample.torque[1],
       sample.torque[2]},
      std::string{netft::to_string(sample.force_unit)},
      std::string{netft::to_string(sample.torque_unit)},
  };
}

} // namespace netft_cli
