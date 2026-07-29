#include "platform/line_reader.hpp"

#include <poll.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace netft_cli {
namespace {

constexpr auto interrupt_poll_interval = std::chrono::milliseconds{10};

class PosixLineReader final : public InterruptibleLineReader {
public:
  explicit PosixLineReader(int file_descriptor) noexcept : file_descriptor_(file_descriptor) {}

  LineReadResult read_line(const InterruptFlag &interrupt) override {
    for (;;) {
      if (interrupt.requested()) {
        return {LineReadStatus::Interrupted, {}};
      }
      if (const auto line = take_line(); line.status == LineReadStatus::Line) {
        return line;
      }

      struct pollfd descriptor{file_descriptor_, POLLIN, 0};
      const int ready = ::poll(&descriptor, 1, static_cast<int>(interrupt_poll_interval.count()));
      if (ready == 0) {
        continue;
      }
      if (ready < 0) {
        if (errno == EINTR) {
          continue;
        }
        return {LineReadStatus::Error, {}};
      }
      if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
        return {LineReadStatus::Error, {}};
      }

      if ((descriptor.revents & (POLLIN | POLLHUP)) != 0) {
        std::array<char, 256> bytes{};
        const auto count = ::read(file_descriptor_, bytes.data(), bytes.size());
        if (count > 0) {
          pending_.append(bytes.data(), static_cast<std::size_t>(count));
          continue;
        }
        if (count == 0) {
          if (pending_.empty()) {
            return {LineReadStatus::Eof, {}};
          }
          auto line = std::move(pending_);
          pending_.clear();
          strip_carriage_return(line);
          return {LineReadStatus::Line, std::move(line)};
        }
        if (errno != EINTR) {
          return {LineReadStatus::Error, {}};
        }
      }
    }
  }

private:
  static void strip_carriage_return(std::string &line) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
  }

  LineReadResult take_line() {
    const auto newline = pending_.find('\n');
    if (newline == std::string::npos) {
      return {LineReadStatus::Eof, {}};
    }
    auto line = pending_.substr(0, newline);
    pending_.erase(0, newline + 1);
    strip_carriage_return(line);
    return {LineReadStatus::Line, std::move(line)};
  }

  int file_descriptor_;
  std::string pending_;
};

} // namespace

std::unique_ptr<InterruptibleLineReader> make_interruptible_line_reader(int file_descriptor) {
  return std::make_unique<PosixLineReader>(file_descriptor);
}

} // namespace netft_cli
