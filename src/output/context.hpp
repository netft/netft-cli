#pragma once

#include <filesystem>
#include <fstream>
#include <istream>
#include <memory>
#include <ostream>

namespace netft_cli {

struct OutputContext {
  std::istream &input;
  std::ostream &standard_output;
  std::ostream &standard_error;
  bool input_is_terminal{};
  bool output_is_terminal{};
};

class OutputHandle {
public:
  static OutputHandle standard(std::ostream &stream);
  static OutputHandle file(const std::filesystem::path &path);

  OutputHandle(OutputHandle &&) noexcept = default;
  OutputHandle &operator=(OutputHandle &&) noexcept = default;
  OutputHandle(const OutputHandle &) = delete;
  OutputHandle &operator=(const OutputHandle &) = delete;

  std::ostream &stream() noexcept { return *stream_; }
  void flush();

private:
  explicit OutputHandle(std::ostream &stream) noexcept : stream_(&stream) {}
  explicit OutputHandle(std::unique_ptr<std::ofstream> stream) noexcept;

  std::unique_ptr<std::ofstream> owned_stream_;
  std::ostream *stream_;
};

} // namespace netft_cli
