#pragma once

#include "cli/options.hpp"
#include "output/context.hpp"
#include "platform/clock.hpp"
#include "platform/interrupt.hpp"
#include "sensor/backend.hpp"

namespace netft_cli {

int run_check(const CheckOptions &options, SensorBackend &backend, OutputContext &output,
              InterruptFlag &interrupt, Clock &clock);

} // namespace netft_cli
