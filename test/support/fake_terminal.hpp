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

  void clear_frame() override {
    operations_.push_back({TerminalOperationType::ClearFrame, {}});
    screen_rows_.clear();
    cursor_row_ = 0;
    cursor_column_ = 0;
  }

  void home() override {
    operations_.push_back({TerminalOperationType::Home, {}});
    cursor_row_ = 0;
    cursor_column_ = 0;
  }

  void write(std::string_view text) override {
    operations_.push_back({TerminalOperationType::Write, std::string{text}});
    for (const char character : text) {
      if (character == '\n') {
        ++cursor_row_;
        cursor_column_ = 0;
        continue;
      }
      if (character == '\r') {
        cursor_column_ = 0;
        continue;
      }
      if (screen_rows_.size() <= cursor_row_) {
        screen_rows_.resize(cursor_row_ + 1);
      }
      auto &row = screen_rows_[cursor_row_];
      if (row.size() <= cursor_column_) {
        row.resize(cursor_column_ + 1, ' ');
      }
      row[cursor_column_] = character;
      ++cursor_column_;
    }
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

  const std::vector<std::string> &screen_rows() const noexcept { return screen_rows_; }

  std::string screen_text() const {
    std::string text;
    for (const auto &row : screen_rows_) {
      text += row;
      text.push_back('\n');
    }
    return text;
  }

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
  std::vector<std::string> screen_rows_;
  std::size_t cursor_row_{};
  std::size_t cursor_column_{};
};

} // namespace netft_cli::test
