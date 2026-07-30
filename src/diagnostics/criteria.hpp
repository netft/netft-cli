#pragma once

#include "diagnostics/result.hpp"

#include <cstdint>
#include <optional>

namespace netft_cli {

struct DiagnosticCriteria {
  std::optional<double> min_rate_hz;
  std::optional<double> max_loss_percent;
  std::optional<std::uint64_t> max_reconnects;
};

[[nodiscard]] DiagnosticResult evaluate_diagnostics(bool configuration_available,
                                                    const StreamHealth &health,
                                                    const DiagnosticCriteria &criteria);

} // namespace netft_cli
