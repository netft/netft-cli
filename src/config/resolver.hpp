#pragma once

#include "cli/options.hpp"
#include "config/environment.hpp"

namespace netft_cli {

[[nodiscard]] Action resolve_options(Action action, const EnvironmentMap &environment);

} // namespace netft_cli
