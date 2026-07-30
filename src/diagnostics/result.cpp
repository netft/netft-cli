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

} // namespace netft_cli
