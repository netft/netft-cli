#include "cli/completion.hpp"

#include "cli/schema.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace netft_cli {
namespace {

std::vector<std::string> completion_words() {
  std::vector<std::string> words{"help", "--help", "--version"};
  for (const auto &command : command_schema().commands) {
    words.emplace_back(command.name);
  }
  for (const auto &option : command_schema().options) {
    words.emplace_back("--" + std::string(option.long_name));
    if (option.short_name != '\0') {
      words.emplace_back("-" + std::string(1, option.short_name));
    }
    for (const auto value : option.values) {
      words.emplace_back(value);
    }
  }
  for (const auto &positional : command_schema().positionals) {
    for (const auto value : positional.values) {
      words.emplace_back(value);
    }
  }
  return words;
}

std::string joined_words() {
  std::string result;
  for (const auto &word : completion_words()) {
    if (!result.empty()) {
      result.push_back(' ');
    }
    result += word;
  }
  return result;
}

} // namespace

std::string render_completion(CompletionShell shell) {
  const auto words = completion_words();
  const auto joined = joined_words();
  std::ostringstream stream;
  switch (shell) {
  case CompletionShell::Bash:
    stream << "_netft_completion() {\n"
              "  COMPREPLY=( $(compgen -W '"
           << joined
           << "' -- \"${COMP_WORDS[COMP_CWORD]}\") )\n"
              "}\n"
              "complete -F _netft_completion netft\n";
    break;
  case CompletionShell::Zsh:
    stream << "#compdef netft\n"
              "_netft() {\n"
              "  local -a values\n"
              "  values=(";
    for (const auto &word : words) {
      stream << " '" << word << '\'';
    }
    stream << " )\n"
              "  _describe 'netft values' values\n"
              "}\n"
              "compdef _netft netft\n";
    break;
  case CompletionShell::Fish:
    for (const auto &word : words) {
      stream << "complete -c netft -a '" << word << "'\n";
    }
    break;
  case CompletionShell::PowerShell:
    stream << "Register-ArgumentCompleter -Native -CommandName netft -ScriptBlock {\n"
              "  param($wordToComplete, $commandAst, $cursorPosition)\n"
              "  @(";
    for (std::size_t index = 0; index < words.size(); ++index) {
      if (index != 0) {
        stream << ", ";
      }
      stream << '\'' << words[index] << '\'';
    }
    stream << ") | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object {\n"
              "    [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', "
              "$_)\n"
              "  }\n"
              "}\n";
    break;
  }
  return stream.str();
}

} // namespace netft_cli
