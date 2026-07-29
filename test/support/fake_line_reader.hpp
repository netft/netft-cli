#pragma once

#include "platform/line_reader.hpp"

#include <cstddef>
#include <functional>
#include <utility>

namespace netft_cli::test {

class FakeLineReader final : public InterruptibleLineReader {
public:
  explicit FakeLineReader(LineReadResult result) : result_(std::move(result)) {}

  LineReadResult read_line(const InterruptFlag & /*interrupt*/) override {
    ++calls_;
    if (on_read_) {
      on_read_();
    }
    return result_;
  }

  void set_on_read(std::function<void()> callback) { on_read_ = std::move(callback); }
  std::size_t calls() const noexcept { return calls_; }

private:
  LineReadResult result_;
  std::function<void()> on_read_;
  std::size_t calls_{};
};

} // namespace netft_cli::test
