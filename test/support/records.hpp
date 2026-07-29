#pragma once

#include "output/records.hpp"

#include <chrono>
#include <cstdint>

namespace netft_cli::test {

inline netft::SensorConfiguration configuration() {
  return {"ATI Mini45",
          {1'000'000.0, 1'000'000.0, netft::ForceUnit::Newton, netft::TorqueUnit::NewtonMillimeter},
          netft::CalibrationSource::Sensor,
          7};
}

inline netft::Sample sample(std::uint32_t sequence = 41) {
  netft::Sample value;
  value.rdt_sequence = sequence;
  value.ft_sequence = sequence - 1;
  value.status = 2;
  value.raw_wrench = {10, -20, 30, -40, 50, -60};
  value.force = {1.25, -2.5, 3.75};
  value.torque = {-4.5, 5.25, -6.75};
  value.force_unit = netft::ForceUnit::Newton;
  value.torque_unit = netft::TorqueUnit::NewtonMillimeter;
  value.configuration_revision = 7;
  value.received_at = std::chrono::steady_clock::time_point{std::chrono::milliseconds{2250}};
  return value;
}

inline netft::HealthSnapshot health() {
  netft::HealthSnapshot value;
  value.state = netft::ClientState::Streaming;
  value.sensor_host = "192.168.1.1";
  value.rdt_port = 49152;
  value.receive_rate_hz = 7000.5;
  value.lost_count = 3;
  value.duplicate_count = 4;
  value.out_of_order_count = 5;
  return value;
}

inline SampleRecord sample_record(std::uint32_t sequence = 41) {
  return make_sample_record(sample(sequence), health(),
                            std::chrono::steady_clock::time_point{std::chrono::milliseconds{1000}});
}

inline SampleRecord next_sample_record() { return sample_record(42); }

} // namespace netft_cli::test
