#include "stream/health_collector.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace netft_cli {
namespace {

std::uint64_t counter_delta(std::uint64_t baseline, std::uint64_t final) noexcept {
  return final >= baseline ? final - baseline : final;
}

} // namespace

HealthCollector::HealthCollector(Clock::TimePoint start, netft::HealthSnapshot baseline)
    : start_(start), baseline_(std::move(baseline)) {}

void HealthCollector::observe(const netft::Sample &sample) noexcept {
  ++sample_count_;
  last_status_ = sample.status;
  if (sample.status != 0) {
    ++nonzero_status_count_;
  }
}

StreamHealth HealthCollector::finish(Clock::TimePoint end,
                                     const netft::HealthSnapshot &final) const noexcept {
  const auto elapsed = std::max(Clock::Duration::zero(), end - start_);
  const double elapsed_seconds = std::chrono::duration<double>{elapsed}.count();
  return {
      elapsed_seconds,
      sample_count_,
      counter_delta(baseline_.received_count, final.received_count),
      elapsed_seconds > 0.0 ? static_cast<double>(sample_count_) / elapsed_seconds : 0.0,
      counter_delta(baseline_.lost_count, final.lost_count),
      counter_delta(baseline_.duplicate_count, final.duplicate_count),
      counter_delta(baseline_.out_of_order_count, final.out_of_order_count),
      counter_delta(baseline_.reconnect_count, final.reconnect_count),
      nonzero_status_count_,
      last_status_,
      final.state,
      final.fault_code,
  };
}

} // namespace netft_cli
