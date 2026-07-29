#pragma once

#include "platform/terminal.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace netft_cli::test {

enum class TerminalOperationType {
  ClearFrame,
  Home,
  Write,
  Flush,
};

struct TerminalOperation {
  TerminalOperationType type;
  std::string text;
};

class FakeTerminal final : public TerminalWriter {
public:
  explicit FakeTerminal(TerminalCapabilities capabilities) : capabilities_(capabilities) {}

  [[nodiscard]] TerminalCapabilities capabilities() const noexcept override {
    return capabilities_;
  }

  void clear_frame() override { operations_.push_back({TerminalOperationType::ClearFrame, {}}); }

  void home() override { operations_.push_back({TerminalOperationType::Home, {}}); }

  void write(std::string_view text) override {
    operations_.push_back({TerminalOperationType::Write, std::string{text}});
  }

  void flush() override { operations_.push_back({TerminalOperationType::Flush, {}}); }

  std::size_t clear_frame_count() const noexcept {
    return count(TerminalOperationType::ClearFrame);
  }

  std::size_t home_count() const noexcept { return count(TerminalOperationType::Home); }

  std::size_t flush_count() const noexcept { return count(TerminalOperationType::Flush); }

  std::vector<std::string> writes() const {
    std::vector<std::string> result;
    for (const auto &operation : operations_) {
      if (operation.type == TerminalOperationType::Write) {
        result.push_back(operation.text);
      }
    }
    return result;
  }

  const std::vector<TerminalOperation> &operations() const noexcept { return operations_; }

private:
  std::size_t count(TerminalOperationType type) const noexcept {
    std::size_t result = 0;
    for (const auto &operation : operations_) {
      if (operation.type == type) {
        ++result;
      }
    }
    return result;
  }

  TerminalCapabilities capabilities_;
  std::vector<TerminalOperation> operations_;
};

} // namespace netft_cli::test
