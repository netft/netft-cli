#pragma once

#include "cli/options.hpp"

#include <netft/types.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace netft_cli {

inline constexpr std::uint32_t machine_schema_version{1};

struct ConfigurationRecord {
  std::string host;
  int http_port{};
  int rdt_port{};
  std::string product_name;
  double counts_per_force_unit{};
  double counts_per_torque_unit{};
  std::string force_unit;
  std::string torque_unit;
  std::string calibration_source;
  std::uint64_t configuration_revision{};
};

struct SampleRecord {
  std::string host;
  double elapsed_seconds{};
  std::uint32_t rdt_sequence{}, ft_sequence{}, status{};
  std::array<std::int32_t, 6> raw{};
  std::array<double, 6> scaled{};
  std::string force_unit, torque_unit;
  double receive_rate_hz{};
  std::uint64_t lost_count{}, duplicate_count{}, out_of_order_count{};
  std::string state;
};

struct BiasRecord {
  ConfigurationRecord configuration;
  SampleRecord before;
  SampleRecord after;
};

ConfigurationRecord make_configuration_record(const ConnectionOptions &options,
                                              const netft::SensorConfiguration &configuration);
SampleRecord make_sample_record(const netft::Sample &sample, const netft::HealthSnapshot &health,
                                std::chrono::steady_clock::time_point origin);

} // namespace netft_cli
