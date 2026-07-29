#include "platform/terminal.hpp"

#include <io.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <memory>
#include <ostream>
#include <string_view>

namespace netft_cli {
namespace {

class WindowsTerminalWriter final : public TerminalWriter {
public:
  WindowsTerminalWriter(std::ostream &stream, int file_descriptor) : stream_(stream) {
    capabilities_.ansi = _isatty(file_descriptor) != 0;
    if (!capabilities_.ansi) {
      return;
    }

    const auto native_descriptor = _get_osfhandle(file_descriptor);
    if (native_descriptor == -1) {
      capabilities_.ansi = false;
      return;
    }
    handle_ = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(native_descriptor));

    CONSOLE_SCREEN_BUFFER_INFO screen_info{};
    if (::GetConsoleScreenBufferInfo(handle_, &screen_info) != FALSE) {
      capabilities_.width =
          static_cast<std::size_t>(screen_info.srWindow.Right - screen_info.srWindow.Left + 1);
      capabilities_.height =
          static_cast<std::size_t>(screen_info.srWindow.Bottom - screen_info.srWindow.Top + 1);
    }

    if (::GetConsoleMode(handle_, &previous_mode_) == FALSE) {
      capabilities_.ansi = false;
      return;
    }
#ifdef ENABLE_VIRTUAL_TERMINAL_PROCESSING
    const DWORD requested_mode = previous_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (::SetConsoleMode(handle_, requested_mode) == FALSE) {
      capabilities_.ansi = false;
      return;
    }
    restore_mode_ = requested_mode != previous_mode_;
#else
    capabilities_.ansi = false;
#endif
  }

  ~WindowsTerminalWriter() override {
    if (restore_mode_) {
      static_cast<void>(::SetConsoleMode(handle_, previous_mode_));
    }
  }

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
  HANDLE handle_{INVALID_HANDLE_VALUE};
  DWORD previous_mode_{};
  bool restore_mode_{false};
  TerminalCapabilities capabilities_;
};

} // namespace

std::unique_ptr<TerminalWriter> make_terminal_writer(std::ostream &stream, int file_descriptor) {
  return std::make_unique<WindowsTerminalWriter>(stream, file_descriptor);
}

} // namespace netft_cli
