#include "platform/terminal.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <memory>
#include <ostream>
#include <string_view>

namespace netft_cli {
namespace {

TerminalCapabilities detect_capabilities(int file_descriptor) {
  TerminalCapabilities capabilities;
  capabilities.ansi = ::isatty(file_descriptor) != 0;
  if (!capabilities.ansi) {
    return capabilities;
  }

  struct winsize size{};
  if (::ioctl(file_descriptor, TIOCGWINSZ, &size) == 0) {
    if (size.ws_col > 0) {
      capabilities.width = size.ws_col;
    }
    if (size.ws_row > 0) {
      capabilities.height = size.ws_row;
    }
  }
  return capabilities;
}

class PosixTerminalWriter final : public TerminalWriter {
public:
  PosixTerminalWriter(std::ostream &stream, int file_descriptor)
      : stream_(stream), capabilities_(detect_capabilities(file_descriptor)) {}

  [[nodiscard]] TerminalCapabilities capabilities() const noexcept override {
    return capabilities_;
  }

  void clear_frame() override {
    if (capabilities_.ansi) {
      stream_ << "\x1b[2J";
    }
  }

  void home() override {
    if (capabilities_.ansi) {
      stream_ << "\x1b[H";
    }
  }

  void write(std::string_view text) override { stream_ << text; }

  void flush() override { stream_.flush(); }

private:
  std::ostream &stream_;
  TerminalCapabilities capabilities_;
};

} // namespace

std::unique_ptr<TerminalWriter> make_terminal_writer(std::ostream &stream, int file_descriptor) {
  return std::make_unique<PosixTerminalWriter>(stream, file_descriptor);
}

} // namespace netft_cli
