#include "cli/parser.hpp"

#include "app/error.hpp"
#include "cli/schema.hpp"
#include "platform/clock.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>

namespace netft_cli {
namespace {

[[noreturn]] void usage_error(const std::string &message) {
  throw AppError(ExitCode::Usage, message);
}

const CommandSpec &find_command(std::string_view name) {
  const auto &schema = command_schema();
  for (const auto &command : schema.commands) {
    if (command.name == name) {
      return command;
    }
  }
  usage_error("Unknown command");
}

const OptionSpec &find_option(std::string_view name) {
  const auto &schema = command_schema();
  for (const auto &option : schema.options) {
    if (option.long_name == name) {
      return option;
    }
  }
  usage_error("Unknown option");
}

const OptionSpec &find_short_option(char name) {
  const auto &schema = command_schema();
  for (const auto &option : schema.options) {
    if (option.short_name == name) {
      return option;
    }
  }
  usage_error("Unknown option");
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

double parse_number(std::string_view value, const std::string &name, bool allow_zero) {
  const std::string text(value);
  char *end{};
  const double number = std::strtod(text.c_str(), &end);
  if (end != text.c_str() + text.size() || !std::isfinite(number) ||
      (allow_zero ? number < 0.0 : number <= 0.0)) {
    usage_error(name + (allow_zero ? " must be a nonnegative finite number"
                                   : " must be a positive finite number"));
  }
  return number;
}

std::uint64_t parse_integer(std::string_view value, const std::string &name, bool allow_zero) {
  std::uint64_t number{};
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
  if (error != std::errc{} || end != value.data() + value.size() || (!allow_zero && number == 0)) {
    usage_error(name +
                (allow_zero ? " must be a nonnegative integer" : " must be a positive integer"));
  }
  return number;
}

void require_representable_clock_duration(double seconds, const std::string &name) {
  const double minimum = std::chrono::duration<double>{Clock::Duration{1}}.count();
  const double maximum = std::chrono::duration<double>{Clock::Duration::max()}.count();
  if (seconds < minimum || seconds >= maximum) {
    usage_error(name + " is outside the supported clock range");
  }
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

  const double seconds = parse_number(number, "Duration", false) * scale;
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    usage_error("Duration must be a positive finite decimal");
  }
  require_representable_clock_duration(seconds, "Duration");
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

bool supports_format(CommandId command, OutputFormat format) {
  if (command == CommandId::Monitor) {
    return format == OutputFormat::Automatic || format == OutputFormat::Table ||
           format == OutputFormat::Ndjson || format == OutputFormat::Csv;
  }
  if (command == CommandId::Record) {
    return format == OutputFormat::Automatic || format == OutputFormat::Ndjson ||
           format == OutputFormat::Csv;
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
  static_cast<void>(find_command(topic));
  return {std::string(topic)};
}

struct ParsedCommand {
  const CommandSpec &spec;
  std::map<OptionId, std::string> values;
  std::set<OptionId> flags;
  std::vector<std::string> positionals;

  [[nodiscard]] bool has(OptionId id) const {
    return values.find(id) != values.end() || flags.find(id) != flags.end();
  }

  [[nodiscard]] std::optional<std::string_view> value(OptionId id) const {
    const auto found = values.find(id);
    if (found == values.end()) {
      return std::nullopt;
    }
    return found->second;
  }
};

ParsedCommand parse_command(const std::vector<std::string_view> &arguments,
                            const CommandSpec &spec) {
  ParsedCommand parsed{spec, {}, {}, {}};
  for (std::size_t index = 1; index < arguments.size(); ++index) {
    const std::string_view argument = arguments[index];
    if (argument == "--help") {
      usage_error("Help must be handled before command parsing");
    }
    if (argument.rfind("--", 0) == 0 || (argument.size() == 2 && argument.front() == '-')) {
      const auto &option = argument.rfind("--", 0) == 0 ? find_option(argument.substr(2))
                                                        : find_short_option(argument[1]);
      if (!spec.accepts(option.id)) {
        usage_error("Option is not available for this command");
      }
      if (parsed.has(option.id) && !option.repeatable) {
        usage_error("Option may only be specified once");
      }
      if (option.value_type == OptionValueType::Flag) {
        parsed.flags.insert(option.id);
      } else {
        parsed.values.emplace(option.id, option_value(arguments, index));
      }
    } else {
      parsed.positionals.emplace_back(argument);
    }
  }
  const auto required = static_cast<std::size_t>(
      std::count_if(spec.positionals.begin(), spec.positionals.end(),
                    [](PositionalId id) { return command_schema().positional(id).required; }));
  if (parsed.positionals.size() < required || parsed.positionals.size() > spec.positionals.size()) {
    usage_error("Command has the wrong number of positional arguments");
  }
  return parsed;
}

ConnectionOptions connection_from(const ParsedCommand &parsed) {
  ConnectionOptions connection;
  if (!parsed.positionals.empty()) {
    connection.host = parsed.positionals.front();
    connection.explicit_values.host = true;
    validate_host(connection.host);
  }
  if (const auto value = parsed.value(OptionId::HttpPort)) {
    connection.http_port = parse_port(*value);
    connection.explicit_values.http_port = true;
  }
  if (const auto value = parsed.value(OptionId::RdtPort)) {
    connection.rdt_port = parse_port(*value);
    connection.explicit_values.rdt_port = true;
  }
  if (const auto value = parsed.value(OptionId::Timeout)) {
    connection.timeout = parse_duration(*value);
    connection.explicit_values.timeout = true;
  }
  return connection;
}

OutputFormat format_from(const ParsedCommand &parsed) {
  const auto value = parsed.value(OptionId::Format);
  const auto format = value.has_value() ? parse_format(*value) : OutputFormat::Automatic;
  if (!supports_format(parsed.spec.id, format)) {
    usage_error("Output format is not supported by this command");
  }
  return format;
}

ColorMode parse_color(std::string_view value) {
  if (value == "auto") {
    return ColorMode::Automatic;
  }
  if (value == "always") {
    return ColorMode::Always;
  }
  if (value == "never") {
    return ColorMode::Never;
  }
  usage_error("Unknown color mode");
}

TerminalOptions terminal_from(const ParsedCommand &parsed) {
  if (parsed.has(OptionId::Verbose) && parsed.has(OptionId::Quiet)) {
    usage_error("Verbose and quiet modes are mutually exclusive");
  }
  TerminalOptions terminal;
  if (parsed.has(OptionId::Verbose)) {
    terminal.verbosity = Verbosity::Verbose;
  } else if (parsed.has(OptionId::Quiet)) {
    terminal.verbosity = Verbosity::Quiet;
  }
  if (const auto value = parsed.value(OptionId::Color)) {
    terminal.color = parse_color(*value);
    terminal.color_explicit = true;
  }
  return terminal;
}

std::optional<std::filesystem::path> optional_output_from(const ParsedCommand &parsed) {
  if (const auto value = parsed.value(OptionId::Output)) {
    return std::filesystem::path(*value);
  }
  return std::nullopt;
}

CompletionShell completion_shell(std::string_view value) {
  if (value == "bash") {
    return CompletionShell::Bash;
  }
  if (value == "zsh") {
    return CompletionShell::Zsh;
  }
  if (value == "fish") {
    return CompletionShell::Fish;
  }
  if (value == "powershell") {
    return CompletionShell::PowerShell;
  }
  usage_error("Unknown completion shell");
}

std::vector<std::string_view>
normalize_global_options(const std::vector<std::string_view> &arguments) {
  std::vector<std::string_view> normalized;
  std::vector<std::string_view> prefix;
  std::size_t index{};
  while (index < arguments.size()) {
    const auto argument = arguments[index];
    if (argument == "--verbose" || argument == "--quiet" || argument == "-v" || argument == "-q") {
      prefix.push_back(argument);
      ++index;
      continue;
    }
    if (argument == "--color") {
      prefix.push_back(argument);
      if (++index == arguments.size()) {
        usage_error("Option requires a value");
      }
      prefix.push_back(arguments[index]);
      ++index;
      continue;
    }
    break;
  }
  if (prefix.empty()) {
    return arguments;
  }
  if (index == arguments.size()) {
    usage_error("Command is required");
  }
  normalized.insert(normalized.end(), arguments.begin() + static_cast<std::ptrdiff_t>(index),
                    arguments.end());
  normalized.insert(normalized.end(), prefix.begin(), prefix.end());
  return normalized;
}

} // namespace

Action parse_arguments(const std::vector<std::string_view> &arguments) {
  const auto normalized = normalize_global_options(arguments);
  if (normalized.empty()) {
    return ShowHelp{"general"};
  }
  if (normalized.front() == "--help") {
    if (normalized.size() != 1) {
      usage_error("General help does not take arguments");
    }
    return ShowHelp{"general"};
  }
  if (normalized.front() == "--schema") {
    if (normalized.size() != 1) {
      usage_error("Schema does not take arguments");
    }
    return ShowHelp{"schema"};
  }
  if (normalized.front() == "--version") {
    if (normalized.size() != 1) {
      usage_error("Version does not take arguments");
    }
    return ShowVersion{};
  }
  if (normalized.front() == "help") {
    if (normalized.size() == 1) {
      return ShowHelp{"general"};
    }
    if (normalized.size() == 2) {
      return help_for(normalized[1]);
    }
    usage_error("Help takes at most one topic");
  }

  const auto &spec = find_command(normalized.front());
  if (normalized.size() == 2 && normalized[1] == "--help") {
    return help_for(spec.name);
  }
  const auto parsed = parse_command(normalized, spec);
  const auto terminal = terminal_from(parsed);

  if (spec.id == CommandId::Completion) {
    return CompletionOptions{completion_shell(parsed.positionals.front()), terminal};
  }

  const auto connection = connection_from(parsed);
  const auto format = format_from(parsed);
  const auto output = optional_output_from(parsed);

  if (spec.id == CommandId::Info) {
    return InfoOptions{connection, format, output, terminal};
  }
  if (spec.id == CommandId::Monitor) {
    double rate_hz = 20.0;
    if (const auto value = parsed.value(OptionId::Rate)) {
      rate_hz = parse_number(*value, "Rate", false);
      const double period_seconds = 1.0 / rate_hz;
      if (!std::isfinite(period_seconds)) {
        usage_error("Rate is outside the supported clock range");
      }
      require_representable_clock_duration(period_seconds, "Rate");
    }
    std::optional<std::chrono::duration<double>> duration;
    if (const auto value = parsed.value(OptionId::Duration)) {
      duration = parse_duration(*value);
    }
    return MonitorOptions{connection, format, output, rate_hz, duration, terminal};
  }
  if (spec.id == CommandId::Check) {
    CheckOptions check;
    check.connection = connection;
    check.format = format;
    check.output = output;
    check.terminal = terminal;
    if (const auto value = parsed.value(OptionId::Duration)) {
      check.duration = parse_duration(*value);
    }
    if (const auto value = parsed.value(OptionId::MinRate)) {
      check.min_rate_hz = parse_number(*value, "Minimum rate", false);
    }
    if (const auto value = parsed.value(OptionId::MaxLoss)) {
      const double loss = parse_number(*value, "Maximum loss", true);
      if (loss > 100.0) {
        usage_error("Maximum loss must not exceed 100 percent");
      }
      check.max_loss_percent = loss;
    }
    if (const auto value = parsed.value(OptionId::MaxReconnects)) {
      check.max_reconnects = parse_integer(*value, "Maximum reconnects", true);
    }
    return check;
  }
  if (spec.id == CommandId::Record) {
    if (!output.has_value() || output->empty()) {
      usage_error("Record requires an output path");
    }
    RecordOptions record;
    record.connection = connection;
    record.format = format;
    record.output = *output;
    record.terminal = terminal;
    if (const auto value = parsed.value(OptionId::Duration)) {
      record.duration = parse_duration(*value);
    }
    if (const auto value = parsed.value(OptionId::Count)) {
      record.count = parse_integer(*value, "Count", false);
    }
    return record;
  }
  return BiasOptions{connection, format, output, parsed.has(OptionId::Yes), terminal};
}

} // namespace netft_cli
