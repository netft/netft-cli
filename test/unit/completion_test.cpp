#include "cli/completion.hpp"

#include "cli/schema.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace netft_cli {
namespace {

TEST(Completion, EveryShellUsesTheSharedCommandAndOptionInventory) {
  const std::array shells{CompletionShell::Bash, CompletionShell::Zsh, CompletionShell::Fish,
                          CompletionShell::PowerShell};
  for (const auto shell : shells) {
    const auto output = render_completion(shell);
    for (const auto &command : command_schema().commands) {
      EXPECT_NE(output.find(command.name), std::string::npos);
    }
    for (const auto &option : command_schema().options) {
      EXPECT_NE(output.find("--" + std::string(option.long_name)), std::string::npos);
      for (const auto value : option.values) {
        EXPECT_NE(output.find(value), std::string::npos);
      }
    }
  }
}

TEST(Completion, EmitsShellSpecificRegistrationEntryPoints) {
  EXPECT_NE(render_completion(CompletionShell::Bash).find("_netft_completion"), std::string::npos);
  EXPECT_NE(render_completion(CompletionShell::Zsh).find("#compdef netft"), std::string::npos);
  EXPECT_NE(render_completion(CompletionShell::Fish).find("complete -c netft"), std::string::npos);
  EXPECT_NE(render_completion(CompletionShell::PowerShell).find("Register-ArgumentCompleter"),
            std::string::npos);
}

} // namespace
} // namespace netft_cli
