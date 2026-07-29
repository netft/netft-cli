#include "cli/options.hpp"

#include "app/error.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <system_error>

namespace netft_cli {
namespace {

[[noreturn]] void usage_error(const std::string &message) {
  throw AppError(ExitCode::Usage, message);
}

std::string option_value(const std::vector<std::string_view> &arguments, std::size_t &index) {
  if (++index == arguments.size()) {
    usage_error("Option requires a value");
  }
  return std::string(arguments[index]);
}

int parse_port(std::string_view value) {
  int port{};
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
  if (error != std::errc{} || end != value.data() + value.size() || port < 1 || port > 65535) {
    usage_error("Port must be in the range 1 through 65535");
  }
  return port;
}

double parse_positive_number(std::string_view value, const std::string &name) {
  const std::string text(value);
  char *end{};
  const double number = std::strtod(text.c_str(), &end);
  if (end != text.c_str() + text.size() || !std::isfinite(number) || number <= 0.0) {
    usage_error(name + " must be a positive finite number");
  }
  return number;
}

std::chrono::duration<double> parse_duration(std::string_view value) {
  std::string_view number = value;
  double scale{};
  if (value.size() > 2 && value.substr(value.size() - 2) == "ms") {
    number.remove_suffix(2);
    scale = 0.001;
  } else if (value.size() > 1 && value.back() == 's') {
    number.remove_suffix(1);
    scale = 1.0;
  } else {
    usage_error("Duration must use ms or s");
  }

  if (number.empty()) {
    usage_error("Duration must be a positive finite decimal");
  }
  bool decimal_point{};
  for (const char character : number) {
    if (character == '.') {
      if (decimal_point) {
        usage_error("Duration must be a positive finite decimal");
      }
      decimal_point = true;
    } else if (character < '0' || character > '9') {
      usage_error("Duration must be a positive finite decimal");
    }
  }
  if (number == "." || number.front() == '.' || number.back() == '.') {
    usage_error("Duration must be a positive finite decimal");
  }

  const double seconds = parse_positive_number(number, "Duration") * scale;
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    usage_error("Duration must be a positive finite decimal");
  }
  return std::chrono::duration<double>(seconds);
}

OutputFormat parse_format(std::string_view value) {
  if (value == "auto") {
    return OutputFormat::Automatic;
  }
  if (value == "text") {
    return OutputFormat::Text;
  }
  if (value == "json") {
    return OutputFormat::Json;
  }
  if (value == "table") {
    return OutputFormat::Table;
  }
  if (value == "ndjson") {
    return OutputFormat::Ndjson;
  }
  if (value == "csv") {
    return OutputFormat::Csv;
  }
  usage_error("Unknown output format");
}

bool supports_format(std::string_view command, OutputFormat format) {
  if (command == "monitor") {
    return format == OutputFormat::Automatic || format == OutputFormat::Table ||
           format == OutputFormat::Ndjson || format == OutputFormat::Csv;
  }
  return format == OutputFormat::Automatic || format == OutputFormat::Text ||
         format == OutputFormat::Json;
}

void validate_host(std::string_view host) {
  if (host.empty() || host.find("://") != std::string_view::npos ||
      host.find_first_of("/?#@") != std::string_view::npos) {
    usage_error("Host must not be a URL");
  }
}

ShowHelp help_for(std::string_view topic) {
  if (topic.empty() || topic == "general") {
    return {"general"};
  }
  if (topic == "info" || topic == "monitor" || topic == "bias") {
    return {std::string(topic)};
  }
  usage_error("Unknown help topic");
}

} // namespace

Action parse_arguments(const std::vector<std::string_view> &arguments) {
  if (arguments.empty()) {
    return ShowHelp{"general"};
  }
  if (arguments.front() == "--help") {
    if (arguments.size() != 1) {
      usage_error("General help does not take arguments");
    }
    return ShowHelp{"general"};
  }
  if (arguments.front() == "--version") {
    if (arguments.size() != 1) {
      usage_error("Version does not take arguments");
    }
    return ShowVersion{};
  }
  if (arguments.front() == "help") {
    if (arguments.size() == 1) {
      return ShowHelp{"general"};
    }
    if (arguments.size() == 2) {
      return help_for(arguments[1]);
    }
    usage_error("Help takes at most one topic");
  }

  const std::string_view command = arguments.front();
  if (command != "info" && command != "monitor" && command != "bias") {
    usage_error("Unknown command");
  }

  ConnectionOptions connection;
  OutputFormat format{OutputFormat::Automatic};
  std::optional<std::filesystem::path> output;
  double rate_hz{20.0};
  std::optional<std::chrono::duration<double>> duration;
  bool assume_yes{};
  bool has_host{};

  for (std::size_t index = 1; index < arguments.size(); ++index) {
    const std::string_view argument = arguments[index];
    if (argument == "--help") {
      return help_for(command);
    }
    if (argument == "--format") {
      format = parse_format(option_value(arguments, index));
    } else if (argument == "--output") {
      output = option_value(arguments, index);
    } else if (argument == "--http-port") {
      connection.http_port = parse_port(option_value(arguments, index));
    } else if (argument == "--rdt-port") {
      connection.rdt_port = parse_port(option_value(arguments, index));
    } else if (argument == "--timeout") {
      connection.timeout = parse_duration(option_value(arguments, index));
    } else if (argument == "--rate") {
      if (command != "monitor") {
        usage_error("Rate is only available for monitor");
      }
      rate_hz = parse_positive_number(option_value(arguments, index), "Rate");
    } else if (argument == "--duration") {
      if (command != "monitor") {
        usage_error("Duration is only available for monitor");
      }
      duration = parse_duration(option_value(arguments, index));
    } else if (argument == "--yes") {
      if (command != "bias") {
        usage_error("Yes is only available for bias");
      }
      assume_yes = true;
    } else if (argument.rfind("--", 0) == 0) {
      usage_error("Unknown option");
    } else if (has_host) {
      usage_error("Only one host is allowed");
    } else {
      connection.host = argument;
      has_host = true;
    }
  }

  if (!has_host) {
    usage_error("Host is required");
  }
  validate_host(connection.host);
  if (!supports_format(command, format)) {
    usage_error("Output format is not supported by this command");
  }

  if (command == "info") {
    return InfoOptions{connection, format, output};
  }
  if (command == "monitor") {
    return MonitorOptions{connection, format, output, rate_hz, duration};
  }
  return BiasOptions{connection, format, output, assume_yes};
}

} // namespace netft_cli
