#include "diagnostics/criteria.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>

namespace netft_cli {
namespace {

StreamHealth healthy_observation() {
  StreamHealth health;
  health.elapsed_seconds = 5.0;
  health.sample_count = 5'000;
  health.received_count = 5'000;
  health.observed_rate_hz = 1'000.0;
  health.state = netft::ClientState::Streaming;
  health.fault_code = netft::FaultCode::None;
  return health;
}

const DiagnosticCheck &find_check(const DiagnosticResult &result, DiagnosticCheckId id) {
  const auto found = std::find_if(result.checks.begin(), result.checks.end(),
                                  [id](const DiagnosticCheck &check) { return check.id == id; });
  if (found == result.checks.end()) {
    throw std::logic_error("diagnostic check is missing");
  }
  return *found;
}

TEST(DiagnosticCriteria, PassesHealthyConfiguredStream) {
  const auto result = evaluate_diagnostics(true, healthy_observation(), {});

  EXPECT_EQ(result.overall, DiagnosticOutcome::Pass);
  EXPECT_EQ(find_check(result, DiagnosticCheckId::Configuration).status, DiagnosticStatus::Pass);
  EXPECT_EQ(find_check(result, DiagnosticCheckId::FirstSample).status, DiagnosticStatus::Pass);
  EXPECT_EQ(find_check(result, DiagnosticCheckId::SustainedStream).status, DiagnosticStatus::Pass);
  EXPECT_EQ(find_check(result, DiagnosticCheckId::SensorStatus).status, DiagnosticStatus::Pass);
}

TEST(DiagnosticCriteria, FailsRequiredCommissioningChecks) {
  auto health = healthy_observation();
  health.sample_count = 0;
  health.state = netft::ClientState::Faulted;
  health.fault_code = netft::FaultCode::Socket;
  health.nonzero_status_count = 1;

  const auto result = evaluate_diagnostics(false, health, {});

  EXPECT_EQ(result.overall, DiagnosticOutcome::Fail);
  EXPECT_EQ(find_check(result, DiagnosticCheckId::Configuration).status, DiagnosticStatus::Fail);
  EXPECT_EQ(find_check(result, DiagnosticCheckId::FirstSample).status, DiagnosticStatus::Fail);
  EXPECT_EQ(find_check(result, DiagnosticCheckId::SustainedStream).status, DiagnosticStatus::Fail);
  EXPECT_EQ(find_check(result, DiagnosticCheckId::SensorStatus).status, DiagnosticStatus::Fail);
}

TEST(DiagnosticCriteria, ReportsPacketLossAsWarningWithoutThreshold) {
  auto health = healthy_observation();
  health.lost_count = 5;

  const auto result = evaluate_diagnostics(true, health, {});

  EXPECT_EQ(result.overall, DiagnosticOutcome::PassWithWarnings);
  EXPECT_EQ(find_check(result, DiagnosticCheckId::PacketLoss).status, DiagnosticStatus::Warning);
}

TEST(DiagnosticCriteria, AppliesRequestedRateLossAndReconnectThresholds) {
  auto health = healthy_observation();
  health.observed_rate_hz = 900.0;
  health.received_count = 990;
  health.lost_count = 10;
  health.reconnect_count = 2;
  DiagnosticCriteria criteria;
  criteria.min_rate_hz = 950.0;
  criteria.max_loss_percent = 0.5;
  criteria.max_reconnects = 1;

  const auto result = evaluate_diagnostics(true, health, criteria);

  EXPECT_EQ(result.overall, DiagnosticOutcome::Fail);
  EXPECT_EQ(find_check(result, DiagnosticCheckId::MinimumRate).status, DiagnosticStatus::Fail);
  EXPECT_EQ(find_check(result, DiagnosticCheckId::MaximumLoss).status, DiagnosticStatus::Fail);
  EXPECT_DOUBLE_EQ(find_check(result, DiagnosticCheckId::MaximumLoss).observed, 1.0);
  EXPECT_EQ(find_check(result, DiagnosticCheckId::MaximumReconnects).status,
            DiagnosticStatus::Fail);
}

} // namespace
} // namespace netft_cli
