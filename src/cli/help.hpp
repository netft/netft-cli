#pragma once

#include <string>
#include <string_view>

namespace netft_cli {

[[nodiscard]] std::string render_help(std::string_view topic);

} // namespace netft_cli
