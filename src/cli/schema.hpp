#pragma once

#include <string_view>
#include <vector>

namespace netft_cli {

enum class CommandId { Info, Monitor, Check, Record, Bias, Completion };

enum class OptionId {
  Format,
  Output,
  HttpPort,
  RdtPort,
  Timeout,
  Rate,
  Duration,
  Yes,
  MinRate,
  MaxLoss,
  MaxReconnects,
  Count,
  Verbose,
  Quiet,
  Color
};

enum class PositionalId { Host, Shell };

enum class OptionValueType {
  Flag,
  Host,
  Port,
  PositiveNumber,
  NonnegativeNumber,
  PositiveInteger,
  NonnegativeInteger,
  Duration,
  Path,
  Choice
};

struct OptionSpec {
  OptionId id;
  std::string_view long_name;
  char short_name{};
  OptionValueType value_type;
  bool repeatable{};
  std::vector<std::string_view> values;
  std::string_view description;
};

struct PositionalSpec {
  PositionalId id;
  std::string_view name;
  OptionValueType value_type;
  bool required{};
  std::vector<std::string_view> values;
};

struct CommandSpec {
  CommandId id;
  std::string_view name;
  std::string_view usage;
  std::string_view description;
  std::vector<OptionId> options;
  std::vector<PositionalId> positionals;
  std::vector<std::string_view> examples;
  std::vector<int> exit_statuses;

  [[nodiscard]] bool accepts(OptionId option) const;
};

struct CommandSchema {
  std::vector<CommandSpec> commands;
  std::vector<OptionSpec> options;
  std::vector<PositionalSpec> positionals;

  [[nodiscard]] const CommandSpec &command(CommandId id) const;
  [[nodiscard]] const OptionSpec &option(OptionId id) const;
  [[nodiscard]] const PositionalSpec &positional(PositionalId id) const;
};

const CommandSchema &command_schema();

} // namespace netft_cli
