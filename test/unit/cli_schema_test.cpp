#include "cli/schema.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string_view>

namespace netft_cli {
namespace {

TEST(CliSchema, RegistersTheCompleteCommandSetOnce) {
  const auto &schema = command_schema();
  std::set<std::string_view> names;
  for (const auto &command : schema.commands) {
    names.insert(command.name);
  }

  EXPECT_EQ(names, (std::set<std::string_view>{"bias", "check", "completion", "info", "monitor",
                                               "record"}));
  EXPECT_EQ(names.size(), schema.commands.size());
}

TEST(CliSchema, DefinesSharedConnectionOptionsOnceWithCommandScopes) {
  const auto &schema = command_schema();
  const auto &http_port = schema.option(OptionId::HttpPort);
  const auto &rdt_port = schema.option(OptionId::RdtPort);
  const auto &timeout = schema.option(OptionId::Timeout);

  EXPECT_EQ(http_port.long_name, "http-port");
  EXPECT_EQ(http_port.value_type, OptionValueType::Port);
  EXPECT_EQ(rdt_port.long_name, "rdt-port");
  EXPECT_EQ(rdt_port.value_type, OptionValueType::Port);
  EXPECT_EQ(timeout.long_name, "timeout");
  EXPECT_EQ(timeout.value_type, OptionValueType::Duration);

  for (const auto command : {CommandId::Info, CommandId::Monitor, CommandId::Check,
                             CommandId::Record, CommandId::Bias}) {
    EXPECT_TRUE(schema.command(command).accepts(OptionId::HttpPort));
    EXPECT_TRUE(schema.command(command).accepts(OptionId::RdtPort));
    EXPECT_TRUE(schema.command(command).accepts(OptionId::Timeout));
  }
  EXPECT_FALSE(schema.command(CommandId::Completion).accepts(OptionId::HttpPort));
}

TEST(CliSchema, DescribesCommandSpecificTypesAndEnumValues) {
  const auto &schema = command_schema();

  EXPECT_EQ(schema.option(OptionId::Rate).value_type, OptionValueType::PositiveNumber);
  EXPECT_EQ(schema.option(OptionId::Duration).value_type, OptionValueType::Duration);
  EXPECT_EQ(schema.option(OptionId::Count).value_type, OptionValueType::PositiveInteger);
  EXPECT_EQ(schema.option(OptionId::Output).value_type, OptionValueType::Path);
  EXPECT_EQ(schema.option(OptionId::Yes).value_type, OptionValueType::Flag);

  const auto &format = schema.option(OptionId::Format);
  EXPECT_EQ(format.value_type, OptionValueType::Choice);
  EXPECT_NE(std::find(format.values.begin(), format.values.end(), "json"), format.values.end());
  EXPECT_NE(std::find(format.values.begin(), format.values.end(), "ndjson"), format.values.end());

  const auto &shell = schema.positional(PositionalId::Shell);
  EXPECT_EQ(shell.value_type, OptionValueType::Choice);
  EXPECT_EQ(shell.values.size(), 4U);
  EXPECT_NE(std::find(shell.values.begin(), shell.values.end(), "powershell"), shell.values.end());
}

TEST(CliSchema, CarriesHelpAndCompletionMetadataInTheSameRegistry) {
  const auto &schema = command_schema();

  for (const auto &command : schema.commands) {
    EXPECT_FALSE(command.usage.empty());
    EXPECT_FALSE(command.description.empty());
    EXPECT_FALSE(command.examples.empty());
    EXPECT_FALSE(command.exit_statuses.empty());
    for (const auto option : command.options) {
      EXPECT_FALSE(schema.option(option).long_name.empty());
    }
  }
}

} // namespace
} // namespace netft_cli
