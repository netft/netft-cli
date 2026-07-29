#include "commands/bias.hpp"

#include "app/error.hpp"
#include "output/confirmation.hpp"
#include "support/assertions.hpp"
#include "support/fake_backend.hpp"
#include "support/fake_confirmation.hpp"
#include "support/fake_line_reader.hpp"
#include "support/memory_output.hpp"
#include "support/options.hpp"
#include "support/records.hpp"
#include "support/structured_parsers.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>

namespace netft_cli {
namespace {

using namespace std::chrono_literals;

void prepare(test::FakeBackend &backend, std::uint32_t before,
             std::vector<netft::Sample> after = {}) {
  backend.set_configuration(test::configuration());
  backend.session().set_samples({test::sample(before)});
  backend.session().set_post_bias_samples(std::move(after));
  backend.session().set_health(test::health());
}

TEST(TerminalConfirmation, AcceptsAffirmativeAfterShowingRawAndScaledPreview) {
  test::MemoryOutput output("", true, true);
  InterruptFlag interrupt;
  test::FakeLineReader reader({LineReadStatus::Line, "yes"});
  TerminalConfirmation confirmation(output.context(), interrupt, reader);
  const BiasPreview preview{
      make_configuration_record(test::connection_options(), test::configuration()),
      test::sample_record(10)};

  EXPECT_TRUE(confirmation.confirm(preview));

  EXPECT_TRUE(output.standard_output_text().empty());
  EXPECT_NE(output.standard_error_text().find("-20"), std::string::npos);
  EXPECT_NE(output.standard_error_text().find("-2.5"), std::string::npos);
  EXPECT_EQ(reader.calls(), 1U);
}

TEST(TerminalConfirmation, RejectsNonAffirmativeInput) {
  test::MemoryOutput output("", true, true);
  InterruptFlag interrupt;
  test::FakeLineReader reader({LineReadStatus::Line, "maybe"});
  TerminalConfirmation confirmation(output.context(), interrupt, reader);
  const BiasPreview preview{
      make_configuration_record(test::connection_options(), test::configuration()),
      test::sample_record(10)};

  EXPECT_FALSE(confirmation.confirm(preview));
}

TEST(TerminalConfirmation, TreatsEndOfInputAsDecline) {
  test::MemoryOutput output("", true, true);
  InterruptFlag interrupt;
  test::FakeLineReader reader({LineReadStatus::Eof, {}});
  TerminalConfirmation confirmation(output.context(), interrupt, reader);
  const BiasPreview preview{
      make_configuration_record(test::connection_options(), test::configuration()),
      test::sample_record(10)};

  EXPECT_FALSE(confirmation.confirm(preview));
}

TEST(TerminalConfirmation, MapsInputReadErrorToIo) {
  test::MemoryOutput output("", true, true);
  InterruptFlag interrupt;
  test::FakeLineReader reader({LineReadStatus::Error, {}});
  TerminalConfirmation confirmation(output.context(), interrupt, reader);
  const BiasPreview preview{
      make_configuration_record(test::connection_options(), test::configuration()),
      test::sample_record(10)};

  test::expect_app_error(ExitCode::Io, [&] { confirmation.confirm(preview); });
}

TEST(BiasCommand, DeclineDoesNotSendBias) {
  test::FakeBackend backend;
  prepare(backend, 10);
  test::MemoryOutput output(true);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  test::expect_app_error(ExitCode::Usage, [&] {
    run_bias(test::bias_options(false), backend, output.context(), confirmation, interrupt);
  });

  EXPECT_EQ(confirmation.calls(), 1U);
  ASSERT_TRUE(confirmation.preview().has_value());
  EXPECT_EQ(confirmation.preview()->sample.rdt_sequence, 10U);
  EXPECT_EQ(confirmation.preview()->configuration.product_name, "ATI Mini45");
  EXPECT_EQ(backend.session().bias_calls(), 0U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  EXPECT_TRUE(output.standard_output_text().empty());
}

TEST(BiasCommand, NonTerminalInputWithoutYesDoesNotPromptOrSendBias) {
  test::FakeBackend backend;
  prepare(backend, 10);
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(true);

  test::expect_app_error(ExitCode::Usage, [&] {
    run_bias(test::bias_options(false), backend, output.context(), confirmation, interrupt);
  });

  EXPECT_EQ(confirmation.calls(), 0U);
  EXPECT_EQ(backend.session().bias_calls(), 0U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  EXPECT_TRUE(output.standard_output_text().empty());
}

TEST(BiasCommand, InterruptedConfirmationDoesNotSendBias) {
  test::FakeBackend backend;
  prepare(backend, 10);
  test::MemoryOutput output(true);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(true);
  confirmation.set_on_confirm([&] { interrupt.request(); });

  EXPECT_EQ(run_bias(test::bias_options(false), backend, output.context(), confirmation, interrupt),
            static_cast<int>(ExitCode::Interrupted));

  EXPECT_EQ(confirmation.calls(), 1U);
  EXPECT_EQ(backend.session().bias_calls(), 0U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  EXPECT_TRUE(output.standard_output_text().empty());
}

TEST(BiasCommand, InterruptedLineReadReturnsWithoutInputAndDoesNotSendBias) {
  test::FakeBackend backend;
  prepare(backend, 10);
  test::MemoryOutput output("", true, true);
  InterruptFlag interrupt;
  test::FakeLineReader reader({LineReadStatus::Interrupted, {}});
  TerminalConfirmation confirmation(output.context(), interrupt, reader);

  EXPECT_EQ(run_bias(test::bias_options(false), backend, output.context(), confirmation, interrupt),
            static_cast<int>(ExitCode::Interrupted));

  EXPECT_EQ(reader.calls(), 1U);
  EXPECT_EQ(backend.session().bias_calls(), 0U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  EXPECT_TRUE(output.standard_output_text().empty());
}

TEST(BiasCommand, YesSkipsPromptAndRequiresLaterSequence) {
  test::FakeBackend backend;
  prepare(backend, 10, {test::sample(10), test::sample(11)});
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  EXPECT_EQ(run_bias(test::bias_options(true), backend, output.context(), confirmation, interrupt),
            0);

  EXPECT_EQ(confirmation.calls(), 0U);
  EXPECT_EQ(backend.session().bias_calls(), 1U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  const auto document = test::parse_json(output.standard_output_text());
  EXPECT_EQ(document.at("configuration").at("product_name"), "ATI Mini45");
  EXPECT_EQ(document.at("before").at("rdt_sequence"), 10U);
  EXPECT_EQ(document.at("after").at("rdt_sequence"), 11U);
}

TEST(BiasCommand, SamePostBiasSequenceTimesOutWithoutOutput) {
  test::FakeBackend backend;
  prepare(backend, 10, {test::sample(10)});
  auto options = test::bias_options(true);
  options.connection.timeout = 1ms;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  test::expect_app_error(ExitCode::Stream, [&] {
    run_bias(options, backend, output.context(), confirmation, interrupt);
  });

  EXPECT_EQ(backend.session().bias_calls(), 1U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  EXPECT_TRUE(output.standard_output_text().empty());
}

TEST(BiasCommand, CallbackWhileBiasCommandIsInProgressIsNotAcceptedAsPostBias) {
  test::FakeBackend backend;
  prepare(backend, 10);
  backend.session().set_during_bias_samples({test::sample(11)});
  auto options = test::bias_options(true);
  options.connection.timeout = 1ms;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  test::expect_app_error(ExitCode::Stream, [&] {
    run_bias(options, backend, output.context(), confirmation, interrupt);
  });

  EXPECT_EQ(backend.session().bias_calls(), 1U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  EXPECT_TRUE(output.standard_output_text().empty());
}

TEST(BiasCommand, FirstSampleFromNewAcquisitionEpochIsAccepted) {
  test::FakeBackend backend;
  prepare(backend, 10);
  auto fresh = test::sample(11);
  fresh.acquisition_epoch = 1;
  backend.session().set_completion_boundary_samples({fresh});
  auto options = test::bias_options(true);
  options.connection.timeout = 1ms;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  EXPECT_EQ(run_bias(options, backend, output.context(), confirmation, interrupt), 0);

  EXPECT_EQ(backend.session().bias_calls(), 1U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  const auto document = test::parse_json(output.standard_output_text());
  EXPECT_EQ(document.at("before").at("rdt_sequence"), 10U);
  EXPECT_EQ(document.at("after").at("rdt_sequence"), 11U);
}

TEST(BiasCommand, SameTickPreBiasCallbackDoesNotProduceSuccess) {
  test::FakeBackend backend;
  prepare(backend, 10);
  const auto boundary = std::chrono::steady_clock::time_point{3s};
  auto stale = test::sample(11);
  stale.received_at = boundary;
  stale.acquisition_epoch = 0;
  backend.session().set_bias_completion_epoch(1);
  backend.session().set_completion_boundary_samples({stale});
  auto options = test::bias_options(true);
  options.connection.timeout = 1ms;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  test::expect_app_error(ExitCode::Stream, [&] {
    run_bias(options, backend, output.context(), confirmation, interrupt);
  });

  EXPECT_EQ(backend.session().bias_calls(), 1U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  EXPECT_TRUE(output.standard_output_text().empty());
}

TEST(BiasCommand, ReceiveStartedBeforeBiasIsRejectedEvenWhenTimestampAndCallbackAreLater) {
  test::FakeBackend backend;
  prepare(backend, 10);
  const auto boundary = std::chrono::steady_clock::time_point{3s};
  auto stale = test::sample(11);
  stale.received_at = boundary + 1s;
  stale.acquisition_epoch = 0;
  backend.session().set_bias_completion_epoch(1);
  backend.session().set_completion_boundary_samples({stale});
  auto options = test::bias_options(true);
  options.connection.timeout = 1ms;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  test::expect_app_error(ExitCode::Stream, [&] {
    run_bias(options, backend, output.context(), confirmation, interrupt);
  });

  EXPECT_EQ(backend.session().bias_calls(), 1U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  EXPECT_TRUE(output.standard_output_text().empty());
}

TEST(BiasCommand, OldInFlightReceiveIsIgnoredBeforeFirstNewEpochSample) {
  test::FakeBackend backend;
  prepare(backend, 10);
  const auto boundary = std::chrono::steady_clock::time_point{3s};
  auto stale = test::sample(11);
  stale.received_at = boundary + 1s;
  stale.acquisition_epoch = 0;
  auto fresh = test::sample(12);
  fresh.received_at = boundary + 2s;
  fresh.acquisition_epoch = 1;
  backend.session().set_bias_completion_epoch(1);
  backend.session().set_completion_boundary_samples({stale, fresh});
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  EXPECT_EQ(run_bias(test::bias_options(true), backend, output.context(), confirmation, interrupt),
            0);

  const auto document = test::parse_json(output.standard_output_text());
  EXPECT_EQ(document.at("before").at("rdt_sequence"), 10U);
  EXPECT_EQ(document.at("after").at("rdt_sequence"), 12U);
  EXPECT_EQ(backend.session().bias_calls(), 1U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(BiasCommand, NoPreBiasSampleDoesNotSendBias) {
  test::FakeBackend backend;
  backend.set_configuration(test::configuration());
  backend.session().set_health(test::health());
  auto options = test::bias_options(true);
  options.connection.timeout = 1ms;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  test::expect_app_error(ExitCode::Stream, [&] {
    run_bias(options, backend, output.context(), confirmation, interrupt);
  });

  EXPECT_EQ(backend.session().bias_calls(), 0U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  EXPECT_TRUE(output.standard_output_text().empty());
}

TEST(BiasCommand, SeriousSensorFaultDoesNotPromptOrSendBias) {
  test::FakeBackend backend;
  prepare(backend, 10);
  auto fault = test::health();
  fault.state = netft::ClientState::Faulted;
  fault.fault_code = netft::FaultCode::SeriousStatus;
  backend.session().set_health(fault);
  test::MemoryOutput output(true);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(true);

  test::expect_app_error(ExitCode::Sensor, [&] {
    run_bias(test::bias_options(false), backend, output.context(), confirmation, interrupt);
  });

  EXPECT_EQ(confirmation.calls(), 0U);
  EXPECT_EQ(backend.session().bias_calls(), 0U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  EXPECT_TRUE(output.standard_output_text().empty());
}

TEST(BiasCommand, BiasFailureMapsToStreamAndStopsWithoutOutput) {
  test::FakeBackend backend;
  prepare(backend, 10);
  backend.session().fail_bias();
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  test::expect_app_error(ExitCode::Stream, [&] {
    run_bias(test::bias_options(true), backend, output.context(), confirmation, interrupt);
  });

  EXPECT_EQ(backend.session().bias_calls(), 1U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
  EXPECT_TRUE(output.standard_output_text().empty());
}

TEST(BiasCommand, TextOutputIsWrittenOnlyAfterSuccess) {
  test::FakeBackend backend;
  prepare(backend, 20, {test::sample(21)});
  auto options = test::bias_options(true);
  options.format = OutputFormat::Text;
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  EXPECT_EQ(run_bias(options, backend, output.context(), confirmation, interrupt), 0);

  EXPECT_NE(output.standard_output_text().find("Before sequence: 20"), std::string::npos);
  EXPECT_NE(output.standard_output_text().find("After sequence: 21"), std::string::npos);
}

TEST(BiasCommand, RejectsUnsupportedFormatBeforeOutputOrNetwork) {
  test::FakeBackend backend;
  auto options = test::bias_options(true);
  options.format = OutputFormat::Table;
  options.output =
      std::filesystem::path(testing::TempDir()) / "netft-bias-invalid-format" / "data.json";
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  test::expect_app_error(ExitCode::Usage, [&] {
    run_bias(options, backend, output.context(), confirmation, interrupt);
  });

  EXPECT_EQ(backend.discover_calls(), 0U);
  EXPECT_EQ(backend.open_calls(), 0U);
  EXPECT_EQ(backend.session().bias_calls(), 0U);
}

TEST(BiasCommand, OpensOutputBeforeContactingSensor) {
  test::FakeBackend backend;
  auto options = test::bias_options(true);
  options.output =
      std::filesystem::path(testing::TempDir()) / "netft-bias-missing-parent" / "data.json";
  test::MemoryOutput output(false);
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  test::expect_app_error(
      ExitCode::Io, [&] { run_bias(options, backend, output.context(), confirmation, interrupt); });

  EXPECT_EQ(backend.discover_calls(), 0U);
  EXPECT_EQ(backend.open_calls(), 0U);
  EXPECT_EQ(backend.session().bias_calls(), 0U);
}

TEST(BiasCommand, RejectsFailedStandardOutputBeforeContactingSensor) {
  test::FakeBackend backend;
  std::istringstream input;
  std::ostringstream standard_output;
  std::ostringstream standard_error;
  standard_output.setstate(std::ios::badbit);
  OutputContext output{input, standard_output, standard_error, false, false};
  InterruptFlag interrupt;
  test::FakeConfirmation confirmation(false);

  test::expect_app_error(ExitCode::Io, [&] {
    run_bias(test::bias_options(true), backend, output, confirmation, interrupt);
  });

  EXPECT_EQ(backend.discover_calls(), 0U);
  EXPECT_EQ(backend.open_calls(), 0U);
  EXPECT_EQ(backend.session().bias_calls(), 0U);
}

} // namespace
} // namespace netft_cli
