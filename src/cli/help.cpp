#include "cli/help.hpp"

#include "app/error.hpp"
#include "cli/schema.hpp"

#include <sstream>
#include <string>

namespace netft_cli {
namespace {

std::string value_label(const OptionSpec &option) {
  if (option.value_type == OptionValueType::Flag) {
    return {};
  }
  if (!option.values.empty()) {
    std::string label;
    for (const auto value : option.values) {
      if (!label.empty()) {
        label.push_back('|');
      }
      label += value;
    }
    return label;
  }
  switch (option.value_type) {
  case OptionValueType::Host:
    return "HOST";
  case OptionValueType::Port:
    return "PORT";
  case OptionValueType::PositiveNumber:
  case OptionValueType::NonnegativeNumber:
    return "NUMBER";
  case OptionValueType::PositiveInteger:
  case OptionValueType::NonnegativeInteger:
    return "INTEGER";
  case OptionValueType::Duration:
    return "DURATION";
  case OptionValueType::Path:
    return "PATH";
  case OptionValueType::Choice:
    return "VALUE";
  case OptionValueType::Flag:
    return {};
  }
  return "VALUE";
}

const CommandSpec &command_named(std::string_view name) {
  for (const auto &command : command_schema().commands) {
    if (command.name == name) {
      return command;
    }
  }
  throw AppError{ExitCode::Usage, "unknown help topic"};
}

std::string general_help() {
  std::ostringstream stream;
  stream << "Usage: netft <command> [options]\n\nCommands:\n";
  for (const auto &command : command_schema().commands) {
    stream << "  " << command.name << "  " << command.description << '\n';
  }
  stream << "\nGlobal options:\n"
            "  -v, --verbose       Show additional diagnostic progress.\n"
            "  -q, --quiet         Suppress progress output.\n"
            "      --color MODE    Control color in human-readable output.\n"
            "      --help          Show help.\n"
            "      --version       Show version.\n";
  return stream.str();
}

} // namespace

std::string render_help(std::string_view topic) {
  if (topic == "general") {
    return general_help();
  }
  const auto &schema = command_schema();
  const auto &command = command_named(topic);
  std::ostringstream stream;
  stream << "Usage: " << command.usage << "\n\n" << command.description << "\n\nOptions:\n";
  for (const auto option_id : command.options) {
    const auto &option = schema.option(option_id);
    stream << "  ";
    if (option.short_name != '\0') {
      stream << '-' << option.short_name << ", ";
    } else {
      stream << "    ";
    }
    stream << "--" << option.long_name;
    const auto label = value_label(option);
    if (!label.empty()) {
      stream << ' ' << label;
    }
    stream << "\n      " << option.description << '\n';
  }
  stream << "\nExamples:\n";
  for (const auto example : command.examples) {
    stream << "  " << example << '\n';
  }
  stream << "\nExit status:\n  ";
  for (std::size_t index = 0; index < command.exit_statuses.size(); ++index) {
    if (index != 0) {
      stream << ", ";
    }
    stream << command.exit_statuses[index];
  }
  stream << '\n';
  return stream.str();
}

} // namespace netft_cli
