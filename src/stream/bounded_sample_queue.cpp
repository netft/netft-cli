#include "stream/bounded_sample_queue.hpp"

#include <stdexcept>
#include <utility>

namespace netft_cli {

BoundedSampleQueue::BoundedSampleQueue(std::size_t capacity) : capacity_(capacity) {
  if (capacity == 0) {
    throw std::invalid_argument("sample queue capacity must be positive");
  }
}

QueuePushResult BoundedSampleQueue::push(const netft::Sample &sample) {
  {
    std::scoped_lock lock(mutex_);
    if (cancelled_) {
      return QueuePushResult::Cancelled;
    }
    if (closed_) {
      return QueuePushResult::Closed;
    }
    if (samples_.size() == capacity_) {
      return QueuePushResult::Overflow;
    }
    samples_.push_back(sample);
  }
  changed_.notify_one();
  return QueuePushResult::Accepted;
}

QueuePopResult BoundedSampleQueue::wait_pop() {
  std::unique_lock lock(mutex_);
  changed_.wait(lock, [&] { return cancelled_ || closed_ || !samples_.empty(); });
  if (cancelled_) {
    return {QueuePopStatus::Cancelled, std::nullopt};
  }
  if (!samples_.empty()) {
    const auto sample = samples_.front();
    samples_.pop_front();
    return {QueuePopStatus::Item, sample};
  }
  return {QueuePopStatus::Closed, std::nullopt};
}

void BoundedSampleQueue::close() noexcept {
  {
    std::scoped_lock lock(mutex_);
    closed_ = true;
  }
  changed_.notify_all();
}

void BoundedSampleQueue::cancel() noexcept {
  {
    std::scoped_lock lock(mutex_);
    cancelled_ = true;
    samples_.clear();
  }
  changed_.notify_all();
}

std::size_t BoundedSampleQueue::size() const noexcept {
  std::scoped_lock lock(mutex_);
  return samples_.size();
}

} // namespace netft_cli
