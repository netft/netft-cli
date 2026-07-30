#pragma once

#include "platform/interrupt.hpp"

#include <netft/types.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace netft_cli {

class LatestSampleSlot {
public:
  void publish(const netft::Sample &sample);
  [[nodiscard]] std::optional<netft::Sample> snapshot() const;
  bool wait_for_first(std::chrono::duration<double> timeout, const InterruptFlag &interrupt);

private:
  mutable std::mutex mutex_;
  std::condition_variable sample_available_;
  std::optional<netft::Sample> sample_;
};

} // namespace netft_cli
