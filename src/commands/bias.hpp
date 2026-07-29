#pragma once

#include "cli/options.hpp"
#include "output/confirmation.hpp"
#include "output/context.hpp"
#include "platform/interrupt.hpp"
#include "sensor/backend.hpp"

namespace netft_cli {

int run_bias(const BiasOptions &options, SensorBackend &backend, OutputContext &output,
             Confirmation &confirmation, InterruptFlag &interrupt);

} // namespace netft_cli
