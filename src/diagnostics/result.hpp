#pragma once

#include "stream/health_collector.hpp"

#include <vector>

namespace netft_cli {

enum class DiagnosticStatus { Pass, Warning, Fail };
enum class DiagnosticOutcome { Pass, PassWithWarnings, Fail };

enum class DiagnosticCheckId {
  Configuration,
  FirstSample,
  SustainedStream,
  SensorStatus,
  PacketLoss,
  MinimumRate,
  MaximumLoss,
  MaximumReconnects,
};

struct DiagnosticCheck {
  DiagnosticCheckId id;
  DiagnosticStatus status;
  double observed{};
  double limit{};
  bool has_limit{};
};

struct DiagnosticResult {
  DiagnosticOutcome overall{DiagnosticOutcome::Pass};
  StreamHealth health;
  std::vector<DiagnosticCheck> checks;
};

[[nodiscard]] DiagnosticOutcome overall_result(const std::vector<DiagnosticCheck> &checks) noexcept;

} // namespace netft_cli
