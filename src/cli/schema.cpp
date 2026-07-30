#include "cli/schema.hpp"

#include <algorithm>
#include <stdexcept>

namespace netft_cli {
namespace {

std::vector<OptionId> with_connection(std::initializer_list<OptionId> command_options) {
  std::vector<OptionId> options{OptionId::HttpPort, OptionId::RdtPort, OptionId::Timeout};
  options.insert(options.end(), command_options.begin(), command_options.end());
  return options;
}

CommandSchema make_schema() {
  return {{
              {CommandId::Info,
               "info",
               "netft info [HOST] [OPTIONS]",
               "Show sensor identity and configuration.",
               with_connection({OptionId::Format, OptionId::Output}),
               {PositionalId::Host},
               {"netft info sensor.example", "netft info sensor.example --format json"},
               {0, 2, 3, 6}},
              {CommandId::Monitor,
               "monitor",
               "netft monitor [HOST] [OPTIONS]",
               "Display the latest sensor sample at a bounded output rate.",
               with_connection(
                   {OptionId::Format, OptionId::Output, OptionId::Rate, OptionId::Duration}),
               {PositionalId::Host},
               {"netft monitor sensor.example", "netft monitor sensor.example --rate 50"},
               {0, 2, 3, 4, 5, 6, 130}},
              {CommandId::Check,
               "check",
               "netft check [HOST] [OPTIONS]",
               "Run a bounded sensor stream health check.",
               with_connection({OptionId::Format, OptionId::Output, OptionId::Duration,
                                OptionId::MinRate, OptionId::MaxLoss, OptionId::MaxReconnects}),
               {PositionalId::Host},
               {"netft check sensor.example", "netft check sensor.example --format json"},
               {0, 2, 3, 4, 5, 6, 7, 130}},
              {CommandId::Record,
               "record",
               "netft record [HOST] --output PATH [OPTIONS]",
               "Record every accepted sensor sample to a bounded capture.",
               with_connection(
                   {OptionId::Format, OptionId::Output, OptionId::Duration, OptionId::Count}),
               {PositionalId::Host},
               {"netft record sensor.example --output measurement.csv",
                "netft record sensor.example --output measurement.ndjson --duration 60s"},
               {0, 2, 3, 4, 5, 6, 8, 130}},
              {CommandId::Bias,
               "bias",
               "netft bias [HOST] [OPTIONS]",
               "Apply a software bias after explicit confirmation.",
               with_connection({OptionId::Format, OptionId::Output, OptionId::Yes}),
               {PositionalId::Host},
               {"netft bias sensor.example", "netft bias sensor.example --yes"},
               {0, 2, 3, 4, 5, 6, 130}},
              {CommandId::Completion,
               "completion",
               "netft completion SHELL",
               "Generate a shell completion script.",
               {},
               {PositionalId::Shell},
               {"netft completion bash", "netft completion powershell"},
               {0, 2}},
          },
          {
              {OptionId::Format,
               "format",
               '\0',
               OptionValueType::Choice,
               false,
               {"auto", "text", "json", "table", "ndjson", "csv"}},
              {OptionId::Output, "output", '\0', OptionValueType::Path, false, {}},
              {OptionId::HttpPort, "http-port", '\0', OptionValueType::Port, false, {}},
              {OptionId::RdtPort, "rdt-port", '\0', OptionValueType::Port, false, {}},
              {OptionId::Timeout, "timeout", '\0', OptionValueType::Duration, false, {}},
              {OptionId::Rate, "rate", '\0', OptionValueType::PositiveNumber, false, {}},
              {OptionId::Duration, "duration", '\0', OptionValueType::Duration, false, {}},
              {OptionId::Yes, "yes", 'y', OptionValueType::Flag, false, {}},
              {OptionId::MinRate, "min-rate", '\0', OptionValueType::PositiveNumber, false, {}},
              {OptionId::MaxLoss, "max-loss", '\0', OptionValueType::NonnegativeNumber, false, {}},
              {OptionId::MaxReconnects,
               "max-reconnects",
               '\0',
               OptionValueType::NonnegativeInteger,
               false,
               {}},
              {OptionId::Count, "count", '\0', OptionValueType::PositiveInteger, false, {}},
          },
          {
              {PositionalId::Host, "HOST", OptionValueType::Host, false, {}},
              {PositionalId::Shell,
               "SHELL",
               OptionValueType::Choice,
               true,
               {"bash", "zsh", "fish", "powershell"}},
          }};
}

} // namespace

bool CommandSpec::accepts(OptionId option) const {
  return std::find(options.begin(), options.end(), option) != options.end();
}

const CommandSpec &CommandSchema::command(CommandId id) const {
  const auto found = std::find_if(commands.begin(), commands.end(),
                                  [id](const auto &item) { return item.id == id; });
  if (found == commands.end()) {
    throw std::logic_error("command is missing from the CLI schema");
  }
  return *found;
}

const OptionSpec &CommandSchema::option(OptionId id) const {
  const auto found = std::find_if(options.begin(), options.end(),
                                  [id](const auto &item) { return item.id == id; });
  if (found == options.end()) {
    throw std::logic_error("option is missing from the CLI schema");
  }
  return *found;
}

const PositionalSpec &CommandSchema::positional(PositionalId id) const {
  const auto found = std::find_if(positionals.begin(), positionals.end(),
                                  [id](const auto &item) { return item.id == id; });
  if (found == positionals.end()) {
    throw std::logic_error("positional is missing from the CLI schema");
  }
  return *found;
}

const CommandSchema &command_schema() {
  static const CommandSchema schema = make_schema();
  return schema;
}

} // namespace netft_cli
