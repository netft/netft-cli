#include "output/records.hpp"

#include <string>

namespace netft_cli {
namespace {

std::string calibration_source(netft::CalibrationSource source) {
  switch (source) {
  case netft::CalibrationSource::Sensor:
    return "sensor";
  case netft::CalibrationSource::Override:
    return "override";
  }
  return "unknown";
}

} // namespace

ConfigurationRecord make_configuration_record(const ConnectionOptions &options,
                                              const netft::SensorConfiguration &configuration) {
  return {options.host,
          options.http_port,
          options.rdt_port,
          configuration.product_name,
          configuration.calibration.counts_per_force_unit,
          configuration.calibration.counts_per_torque_unit,
          std::string{netft::to_string(configuration.calibration.force_unit)},
          std::string{netft::to_string(configuration.calibration.torque_unit)},
          calibration_source(configuration.source),
          configuration.revision};
}

SampleRecord make_sample_record(const netft::Sample &sample, const netft::HealthSnapshot &health,
                                std::chrono::steady_clock::time_point origin) {
  return {health.sensor_host,
          std::chrono::duration<double>{sample.received_at - origin}.count(),
          sample.rdt_sequence,
          sample.ft_sequence,
          sample.status,
          sample.raw_wrench,
          {sample.force[0], sample.force[1], sample.force[2], sample.torque[0], sample.torque[1],
           sample.torque[2]},
          std::string{netft::to_string(sample.force_unit)},
          std::string{netft::to_string(sample.torque_unit)},
          health.receive_rate_hz,
          health.lost_count,
          health.duplicate_count,
          health.out_of_order_count,
          std::string{netft::to_string(health.state)}};
}

} // namespace netft_cli
