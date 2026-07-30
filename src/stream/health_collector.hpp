#pragma once

#include "platform/clock.hpp"

#include <netft/types.hpp>

#include <cstdint>

namespace netft_cli {

struct StreamHealth {
  double elapsed_seconds{};
  std::uint64_t sample_count{};
  std::uint64_t received_count{};
  double observed_rate_hz{};
  std::uint64_t lost_count{};
  std::uint64_t duplicate_count{};
  std::uint64_t out_of_order_count{};
  std::uint64_t reconnect_count{};
  std::uint64_t nonzero_status_count{};
  std::uint32_t last_status{};
  netft::ClientState state{netft::ClientState::Stopped};
  netft::FaultCode fault_code{netft::FaultCode::None};
};

class HealthCollector {
public:
  HealthCollector(Clock::TimePoint start, netft::HealthSnapshot baseline);

  void observe(const netft::Sample &sample) noexcept;
  [[nodiscard]] StreamHealth finish(Clock::TimePoint end,
                                    const netft::HealthSnapshot &final) const noexcept;

private:
  Clock::TimePoint start_;
  netft::HealthSnapshot baseline_;
  std::uint64_t sample_count_{};
  std::uint64_t nonzero_status_count_{};
  std::uint32_t last_status_{};
};

} // namespace netft_cli
