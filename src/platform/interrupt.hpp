#pragma once

#include <atomic>
#include <memory>

namespace netft_cli {

class InterruptFlag {
public:
  void request() noexcept { requested_.store(true, std::memory_order_release); }

  [[nodiscard]] bool requested() const noexcept {
    return requested_.load(std::memory_order_acquire);
  }

private:
  std::atomic<bool> requested_{false};
};

class InterruptHandler {
public:
  explicit InterruptHandler(InterruptFlag &flag);
  ~InterruptHandler();

  InterruptHandler(const InterruptHandler &) = delete;
  InterruptHandler &operator=(const InterruptHandler &) = delete;
  InterruptHandler(InterruptHandler &&) = delete;
  InterruptHandler &operator=(InterruptHandler &&) = delete;

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace netft_cli
