#include "config/environment.hpp"

#include <array>
#include <cstdlib>

namespace netft_cli {

EnvironmentMap read_process_environment() {
  constexpr std::array names{"NETFT_HOST", "NETFT_HTTP_PORT", "NETFT_RDT_PORT", "NETFT_TIMEOUT",
                             "NO_COLOR"};
  EnvironmentMap environment;
  for (const char *name : names) {
    if (const char *value = std::getenv(name)) {
      environment.emplace(name, value);
    }
  }
  return environment;
}

} // namespace netft_cli
