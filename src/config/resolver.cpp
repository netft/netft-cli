#include "config/resolver.hpp"

#include "app/error.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

namespace netft_cli {
namespace {

[[noreturn]] void environment_error(std::string_view variable, std::string_view requirement) {
  throw AppError{ExitCode::Usage, std::string(variable) + " " + std::string(requirement)};
}

[[nodiscard]] std::optional<std::string_view> environment_value(const EnvironmentMap &environment,
                                                                std::string_view name) {
  const auto found = environment.find(std::string(name));
  if (found == environment.end()) {
    return std::nullopt;
  }
  return found->second;
}

int environment_port(std::string_view value, std::string_view variable) {
  int port{};
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
  if (error != std::errc{} || end != value.data() + value.size() || port < 1 || port > 65535) {
    environment_error(variable, "must be an integer from 1 through 65535");
  }
  return port;
}

std::chrono::duration<double> environment_duration(std::string_view value,
                                                   std::string_view variable) {
  double scale{};
  if (value.size() > 2 && value.substr(value.size() - 2) == "ms") {
    value.remove_suffix(2);
    scale = 0.001;
  } else if (value.size() > 1 && value.back() == 's') {
    value.remove_suffix(1);
    scale = 1.0;
  } else {
    environment_error(variable, "must be a positive duration using ms or s");
  }
  const std::string text(value);
  char *end{};
  const double number = std::strtod(text.c_str(), &end);
  const double seconds = number * scale;
  if (end != text.c_str() + text.size() || !std::isfinite(seconds) || seconds <= 0.0) {
    environment_error(variable, "must be a positive duration using ms or s");
  }
  return std::chrono::duration<double>{seconds};
}

void validate_host(std::string_view host, std::string_view source) {
  if (host.empty() || host.find("://") != std::string_view::npos ||
      host.find_first_of("/?#@") != std::string_view::npos) {
    environment_error(source, "must name a host, not a URL");
  }
}

bool machine_format(OutputFormat format) {
  return format == OutputFormat::Json || format == OutputFormat::Ndjson ||
         format == OutputFormat::Csv;
}

void resolve_connection(ConnectionOptions &connection, const EnvironmentMap &environment) {
  if (!connection.explicit_values.host) {
    if (const auto value = environment_value(environment, "NETFT_HOST")) {
      connection.host = *value;
    }
  }
  if (connection.host.empty()) {
    throw AppError{ExitCode::Usage, "Host is required as an argument or through NETFT_HOST"};
  }
  validate_host(connection.host, connection.explicit_values.host ? "Host" : "NETFT_HOST");

  if (const auto value = environment_value(environment, "NETFT_HTTP_PORT");
      !connection.explicit_values.http_port && value) {
    connection.http_port = environment_port(*value, "NETFT_HTTP_PORT");
  }
  if (const auto value = environment_value(environment, "NETFT_RDT_PORT");
      !connection.explicit_values.rdt_port && value) {
    connection.rdt_port = environment_port(*value, "NETFT_RDT_PORT");
  }
  if (const auto value = environment_value(environment, "NETFT_TIMEOUT");
      !connection.explicit_values.timeout && value) {
    connection.timeout = environment_duration(*value, "NETFT_TIMEOUT");
  }
}

template <typename Options>
void resolve_command(Options &options, const EnvironmentMap &environment) {
  resolve_connection(options.connection, environment);
  if (machine_format(options.format) ||
      (!options.terminal.color_explicit && environment.find("NO_COLOR") != environment.end())) {
    options.terminal.color = ColorMode::Never;
  }
}

} // namespace

Action resolve_options(Action action, const EnvironmentMap &environment) {
  std::visit(
      [&](auto &value) {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, InfoOptions> || std::is_same_v<Value, MonitorOptions> ||
                      std::is_same_v<Value, CheckOptions> || std::is_same_v<Value, RecordOptions> ||
                      std::is_same_v<Value, BiasOptions>) {
          resolve_command(value, environment);
        }
      },
      action);
  return action;
}

} // namespace netft_cli
