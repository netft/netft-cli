#pragma once

#include "cli/options.hpp"

#include <chrono>

namespace netft_cli::test {

inline ConnectionOptions connection_options() {
  return {"192.168.1.1", 80, 49152, std::chrono::duration<double>{0.25}};
}

inline InfoOptions info_options() {
  return {connection_options(), OutputFormat::Json, std::nullopt};
}

inline MonitorOptions monitor_for(std::chrono::duration<double> duration) {
  return {connection_options(), OutputFormat::Ndjson, std::nullopt, 20.0, duration};
}

inline BiasOptions bias_options(bool assume_yes) {
  return {connection_options(), OutputFormat::Json, std::nullopt, assume_yes};
}

} // namespace netft_cli::test
