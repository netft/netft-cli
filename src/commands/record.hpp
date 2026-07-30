#pragma once

#include "cli/options.hpp"
#include "output/context.hpp"
#include "platform/clock.hpp"
#include "platform/filesystem.hpp"
#include "platform/interrupt.hpp"
#include "sensor/backend.hpp"

#include <cstddef>

namespace netft_cli {

int run_record(const RecordOptions &options, SensorBackend &backend, OutputContext &output,
               InterruptFlag &interrupt, Clock &clock, WallClock &wall_clock,
               Filesystem &filesystem, std::size_t queue_capacity = 8192);

} // namespace netft_cli
