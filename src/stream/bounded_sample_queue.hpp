#pragma once

#include <netft/types.hpp>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace netft_cli {

enum class QueuePushResult { Accepted, Overflow, Closed, Cancelled };
enum class QueuePopStatus { Item, Closed, Cancelled };

struct QueuePopResult {
  QueuePopStatus status;
  std::optional<netft::Sample> sample;
};

class BoundedSampleQueue {
public:
  explicit BoundedSampleQueue(std::size_t capacity);

  [[nodiscard]] QueuePushResult push(const netft::Sample &sample);
  [[nodiscard]] QueuePopResult wait_pop();
  void close() noexcept;
  void cancel() noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<netft::Sample> samples_;
  bool closed_{};
  bool cancelled_{};
};

} // namespace netft_cli
