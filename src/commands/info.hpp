#pragma once

#include "cli/options.hpp"
#include "output/context.hpp"
#include "sensor/backend.hpp"

namespace netft_cli {

int run_info(const InfoOptions &options, SensorBackend &backend, OutputContext &output);

} // namespace netft_cli
