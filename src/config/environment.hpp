#pragma once

#include <map>
#include <string>

namespace netft_cli {

using EnvironmentMap = std::map<std::string, std::string>;

[[nodiscard]] EnvironmentMap read_process_environment();

} // namespace netft_cli
