#pragma once

#include "output/confirmation.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>

namespace netft_cli::test {

class FakeConfirmation final : public Confirmation {
public:
  explicit FakeConfirmation(bool decision) noexcept : decision_(decision) {}

  bool confirm(const BiasPreview &preview) override {
    ++calls_;
    preview_ = preview;
    if (on_confirm_) {
      on_confirm_();
    }
    return decision_;
  }

  void set_on_confirm(std::function<void()> callback) { on_confirm_ = std::move(callback); }

  std::size_t calls() const noexcept { return calls_; }
  const std::optional<BiasPreview> &preview() const noexcept { return preview_; }

private:
  bool decision_;
  std::size_t calls_{};
  std::optional<BiasPreview> preview_;
  std::function<void()> on_confirm_;
};

} // namespace netft_cli::test
