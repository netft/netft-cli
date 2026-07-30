#include "diagnostics/result.hpp"

namespace netft_cli {

DiagnosticOutcome overall_result(const std::vector<DiagnosticCheck> &checks) noexcept {
  auto outcome = DiagnosticOutcome::Pass;
  for (const auto &check : checks) {
    if (check.status == DiagnosticStatus::Fail) {
      return DiagnosticOutcome::Fail;
    }
    if (check.status == DiagnosticStatus::Warning) {
      outcome = DiagnosticOutcome::PassWithWarnings;
    }
  }
  return outcome;
}

std::string_view diagnostic_name(DiagnosticCheckId id) noexcept {
  switch (id) {
  case DiagnosticCheckId::Configuration:
    return "configuration";
  case DiagnosticCheckId::FirstSample:
    return "first_sample";
  case DiagnosticCheckId::SustainedStream:
    return "sustained_stream";
  case DiagnosticCheckId::SensorStatus:
    return "sensor_status";
  case DiagnosticCheckId::PacketLoss:
    return "packet_loss";
  case DiagnosticCheckId::MinimumRate:
    return "minimum_rate";
  case DiagnosticCheckId::MaximumLoss:
    return "maximum_loss";
  case DiagnosticCheckId::MaximumReconnects:
    return "maximum_reconnects";
  }
  return "unknown";
}

std::string_view diagnostic_status_name(DiagnosticStatus status) noexcept {
  switch (status) {
  case DiagnosticStatus::Pass:
    return "pass";
  case DiagnosticStatus::Warning:
    return "warning";
  case DiagnosticStatus::Fail:
    return "fail";
  }
  return "fail";
}

std::string_view diagnostic_outcome_name(DiagnosticOutcome outcome) noexcept {
  switch (outcome) {
  case DiagnosticOutcome::Pass:
    return "pass";
  case DiagnosticOutcome::PassWithWarnings:
    return "pass_with_warnings";
  case DiagnosticOutcome::Fail:
    return "fail";
  }
  return "fail";
}

} // namespace netft_cli
