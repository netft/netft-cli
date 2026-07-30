#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace netft_cli {

enum class OutputFormat { Automatic, Text, Json, Table, Ndjson, Csv };

enum class Verbosity { Normal, Verbose, Quiet };

enum class ColorMode { Automatic, Always, Never };

struct TerminalOptions {
  Verbosity verbosity{Verbosity::Normal};
  ColorMode color{ColorMode::Automatic};
  bool color_explicit{};
};

struct ConnectionOverrides {
  bool host{};
  bool http_port{};
  bool rdt_port{};
  bool timeout{};
};

struct ConnectionOptions {
  std::string host;
  int http_port{80};
  int rdt_port{49152};
  std::chrono::duration<double> timeout{1.0};
  ConnectionOverrides explicit_values;
};

struct ShowHelp {
  std::string topic;
};

struct ShowVersion {};

struct InfoOptions {
  ConnectionOptions connection;
  OutputFormat format;
  std::optional<std::filesystem::path> output;
  TerminalOptions terminal;
};

struct MonitorOptions {
  ConnectionOptions connection;
  OutputFormat format{OutputFormat::Automatic};
  std::optional<std::filesystem::path> output;
  double rate_hz{20.0};
  std::optional<std::chrono::duration<double>> duration;
  TerminalOptions terminal;
};

struct BiasOptions {
  ConnectionOptions connection;
  OutputFormat format{OutputFormat::Automatic};
  std::optional<std::filesystem::path> output;
  bool assume_yes{false};
  TerminalOptions terminal;
};

struct CheckOptions {
  ConnectionOptions connection;
  OutputFormat format{OutputFormat::Automatic};
  std::optional<std::filesystem::path> output;
  std::chrono::duration<double> duration{5.0};
  std::optional<double> min_rate_hz;
  std::optional<double> max_loss_percent;
  std::optional<std::uint64_t> max_reconnects;
  TerminalOptions terminal;
};

struct RecordOptions {
  ConnectionOptions connection;
  OutputFormat format{OutputFormat::Automatic};
  std::filesystem::path output;
  std::optional<std::chrono::duration<double>> duration;
  std::optional<std::uint64_t> count;
  TerminalOptions terminal;
};

enum class CompletionShell { Bash, Zsh, Fish, PowerShell };

struct CompletionOptions {
  CompletionShell shell;
  TerminalOptions terminal;
};

using Action = std::variant<ShowHelp, ShowVersion, InfoOptions, MonitorOptions, CheckOptions,
                            RecordOptions, BiasOptions, CompletionOptions>;

} // namespace netft_cli
