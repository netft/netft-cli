#include "monitor/latest_sample.hpp"

#include <algorithm>

namespace netft_cli {

void LatestSampleSlot::publish(const netft::Sample &sample) {
  {
    std::scoped_lock lock(mutex_);
    sample_ = sample;
  }
  sample_available_.notify_all();
}

std::optional<netft::Sample> LatestSampleSlot::snapshot() const {
  std::scoped_lock lock(mutex_);
  return sample_;
}

bool LatestSampleSlot::wait_for_first(std::chrono::duration<double> timeout,
                                      const InterruptFlag &interrupt) {
  constexpr auto interrupt_poll_interval = std::chrono::milliseconds{10};
  const auto timeout_ticks =
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
  const auto deadline = std::chrono::steady_clock::now() + timeout_ticks;
  std::unique_lock lock(mutex_);
  while (!sample_ && !interrupt.requested()) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      break;
    }
    sample_available_.wait_until(lock, std::min(deadline, now + interrupt_poll_interval));
  }
  return sample_.has_value();
}

} // namespace netft_cli
