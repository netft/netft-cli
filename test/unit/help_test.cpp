#include "cli/help.hpp"

#include "cli/schema.hpp"

#include <gtest/gtest.h>

#include <string>

namespace netft_cli {
namespace {

TEST(Help, GeneralTopicListsEveryCommandFromSchema) {
  const auto output = render_help("general");
  for (const auto &command : command_schema().commands) {
    EXPECT_NE(output.find(command.name), std::string::npos);
  }
}

TEST(Help, CommandTopicsExposeSchemaSemantics) {
  const auto &schema = command_schema();
  for (const auto &command : schema.commands) {
    const auto output = render_help(command.name);
    EXPECT_NE(output.find(command.usage), std::string::npos);
    EXPECT_NE(output.find("Options:"), std::string::npos);
    EXPECT_NE(output.find("Examples:"), std::string::npos);
    EXPECT_NE(output.find("Exit status:"), std::string::npos);
    for (const auto option : command.options) {
      EXPECT_NE(output.find("--" + std::string(schema.option(option).long_name)),
                std::string::npos);
    }
    for (const auto example : command.examples) {
      EXPECT_NE(output.find(example), std::string::npos);
    }
  }
}

} // namespace
} // namespace netft_cli
