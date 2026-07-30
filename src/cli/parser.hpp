#pragma once

#include "cli/options.hpp"

#include <string_view>
#include <vector>

namespace netft_cli {

Action parse_arguments(const std::vector<std::string_view> &arguments);

} // namespace netft_cli
