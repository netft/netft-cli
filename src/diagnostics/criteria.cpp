#include "diagnostics/criteria.hpp"

#include <utility>
#include <vector>

namespace netft_cli {
namespace {

DiagnosticStatus pass_if(bool condition) noexcept {
  return condition ? DiagnosticStatus::Pass : DiagnosticStatus::Fail;
}

double loss_percent(const StreamHealth &health) noexcept {
  const auto total = health.received_count + health.lost_count;
  return total == 0 ? 0.0
                    : 100.0 * static_cast<double>(health.lost_count) / static_cast<double>(total);
}

} // namespace

DiagnosticResult evaluate_diagnostics(bool configuration_available, const StreamHealth &health,
                                      const DiagnosticCriteria &criteria) {
  std::vector<DiagnosticCheck> checks{
      {DiagnosticCheckId::Configuration, pass_if(configuration_available)},
      {DiagnosticCheckId::FirstSample, pass_if(health.sample_count != 0),
       static_cast<double>(health.sample_count)},
      {DiagnosticCheckId::SustainedStream, pass_if(health.state == netft::ClientState::Streaming &&
                                                   health.fault_code == netft::FaultCode::None)},
      {DiagnosticCheckId::SensorStatus, pass_if(health.nonzero_status_count == 0),
       static_cast<double>(health.nonzero_status_count)},
  };

  const double observed_loss = loss_percent(health);
  if (criteria.min_rate_hz) {
    checks.push_back({DiagnosticCheckId::MinimumRate,
                      pass_if(health.observed_rate_hz >= *criteria.min_rate_hz),
                      health.observed_rate_hz, *criteria.min_rate_hz, true});
  }
  if (criteria.max_loss_percent) {
    checks.push_back({DiagnosticCheckId::MaximumLoss,
                      pass_if(observed_loss <= *criteria.max_loss_percent), observed_loss,
                      *criteria.max_loss_percent, true});
  } else if (health.lost_count != 0) {
    checks.push_back({DiagnosticCheckId::PacketLoss, DiagnosticStatus::Warning, observed_loss});
  }
  if (criteria.max_reconnects) {
    checks.push_back({DiagnosticCheckId::MaximumReconnects,
                      pass_if(health.reconnect_count <= *criteria.max_reconnects),
                      static_cast<double>(health.reconnect_count),
                      static_cast<double>(*criteria.max_reconnects), true});
  }

  const auto overall = overall_result(checks);
  return {overall, health, std::move(checks)};
}

} // namespace netft_cli
