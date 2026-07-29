#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace netft_cli {

enum class OutputFormat { Automatic, Text, Json, Table, Ndjson, Csv };

struct ConnectionOptions {
  std::string host;
  int http_port{80};
  int rdt_port{49152};
  std::chrono::duration<double> timeout{1.0};
};

struct ShowHelp {
  std::string topic;
};

struct ShowVersion {};

struct InfoOptions {
  ConnectionOptions connection;
  OutputFormat format;
  std::optional<std::filesystem::path> output;
};

struct MonitorOptions {
  ConnectionOptions connection;
  OutputFormat format{OutputFormat::Automatic};
  std::optional<std::filesystem::path> output;
  double rate_hz{20.0};
  std::optional<std::chrono::duration<double>> duration;
};

struct BiasOptions {
  ConnectionOptions connection;
  OutputFormat format{OutputFormat::Automatic};
  std::optional<std::filesystem::path> output;
  bool assume_yes{false};
};

using Action = std::variant<ShowHelp, ShowVersion, InfoOptions, MonitorOptions, BiasOptions>;

Action parse_arguments(const std::vector<std::string_view> &arguments);

} // namespace netft_cli
