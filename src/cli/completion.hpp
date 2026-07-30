#pragma once

#include "cli/options.hpp"

#include <string>

namespace netft_cli {

[[nodiscard]] std::string render_completion(CompletionShell shell);

} // namespace netft_cli
