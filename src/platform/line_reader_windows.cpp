#include "platform/line_reader.hpp"

#include <io.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace netft_cli {
namespace {

constexpr auto interrupt_poll_interval = std::chrono::milliseconds{10};

class WindowsLineReader final : public InterruptibleLineReader {
public:
  explicit WindowsLineReader(int file_descriptor) noexcept {
    const auto native_descriptor = _get_osfhandle(file_descriptor);
    if (native_descriptor == -1) {
      return;
    }
    handle_ = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(native_descriptor));
    DWORD mode{};
    valid_ = ::GetConsoleMode(handle_, &mode) != FALSE;
  }

  LineReadResult read_line(const InterruptFlag &interrupt) override {
    if (!valid_) {
      return {LineReadStatus::Error, {}};
    }

    std::string line;
    for (;;) {
      if (interrupt.requested()) {
        return {LineReadStatus::Interrupted, {}};
      }
      const DWORD ready =
          ::WaitForSingleObject(handle_, static_cast<DWORD>(interrupt_poll_interval.count()));
      if (ready == WAIT_TIMEOUT) {
        continue;
      }
      if (ready != WAIT_OBJECT_0) {
        return {LineReadStatus::Error, {}};
      }

      INPUT_RECORD record{};
      DWORD count{};
      if (::ReadConsoleInputW(handle_, &record, 1, &count) == FALSE) {
        return {LineReadStatus::Error, {}};
      }
      if (count == 0 || record.EventType != KEY_EVENT || record.Event.KeyEvent.bKeyDown == FALSE) {
        continue;
      }

      const wchar_t character = record.Event.KeyEvent.uChar.UnicodeChar;
      if (character == L'\r' || character == L'\n') {
        return {LineReadStatus::Line, std::move(line)};
      }
      if (character == L'\b') {
        if (!line.empty()) {
          line.pop_back();
        }
        continue;
      }
      if (character == 0x1A) {
        if (line.empty()) {
          return {LineReadStatus::Eof, {}};
        }
        return {LineReadStatus::Line, std::move(line)};
      }
      if (character >= 0x20 && character <= 0x7E) {
        line.push_back(static_cast<char>(character));
      }
    }
  }

private:
  HANDLE handle_{INVALID_HANDLE_VALUE};
  bool valid_{};
};

} // namespace

std::unique_ptr<InterruptibleLineReader> make_interruptible_line_reader(int file_descriptor) {
  return std::make_unique<WindowsLineReader>(file_descriptor);
}

} // namespace netft_cli
