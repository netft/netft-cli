#pragma once

#include <cstddef>
#include <memory>
#include <ostream>
#include <string_view>

namespace netft_cli {

struct TerminalCapabilities {
  std::size_t width{80};
  std::size_t height{24};
  bool ansi{false};
};

class TerminalWriter {
public:
  virtual ~TerminalWriter() = default;

  [[nodiscard]] virtual TerminalCapabilities capabilities() const noexcept = 0;
  virtual void clear_frame() = 0;
  virtual void home() = 0;
  virtual void write(std::string_view text) = 0;
  virtual void flush() = 0;
};

std::unique_ptr<TerminalWriter> make_terminal_writer(std::ostream &stream, int file_descriptor);

} // namespace netft_cli
