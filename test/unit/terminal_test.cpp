#include "output/terminal.hpp"
#include "support/fake_terminal.hpp"
#include "support/options.hpp"
#include "support/records.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace netft_cli {
namespace {

std::vector<std::string_view> lines(const std::string &text) {
  std::vector<std::string_view> result;
  std::size_t start = 0;
  while (start < text.size()) {
    const auto end = text.find('\n', start);
    result.emplace_back(text.data() + start,
                        (end == std::string::npos ? text.size() : end) - start);
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return result;
}

std::string_view line_starting_with(const std::string &text, std::string_view prefix) {
  const auto rendered_lines = lines(text);
  const auto found =
      std::find_if(rendered_lines.begin(), rendered_lines.end(), [prefix](std::string_view line) {
        return line.substr(0, prefix.size()) == prefix;
      });
  return found == rendered_lines.end() ? std::string_view{} : *found;
}

bool contains_unsafe_terminal_control(std::string_view text) {
  return std::any_of(text.begin(), text.end(), [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return (byte < 0x20U && character != '\n') || byte == 0x7FU;
  });
}

TEST(TerminalMonitor, ReusesOneFrameAndDoesNotAppendHistory) {
  test::FakeTerminal terminal(TerminalCapabilities{100, 24, true});
  TerminalMonitor monitor(terminal);
  monitor.render(test::sample_record(1));
  monitor.render(test::sample_record(2));

  EXPECT_EQ(terminal.clear_frame_count(), 1U);
  EXPECT_EQ(terminal.home_count(), 2U);
  EXPECT_EQ(terminal.writes().size(), 2U);
}

TEST(TerminalMonitor, KeepsRawAndConvertedColumnsFixedAcrossMagnitudes) {
  test::FakeTerminal terminal(TerminalCapabilities{100, 24, true});
  TerminalMonitor monitor(terminal);
  auto small = test::sample_record(1);
  small.raw = {1, -2, 3, -4, 5, -6};
  small.scaled = {0.1, -0.2, 0.3, -0.4, 0.5, -0.6};
  auto large = small;
  large.raw = {123456789, -234567890, 345678901, -456789012, 567890123, -678901234};
  large.scaled = {12345.125, -23456.25, 34567.375, -45678.5, 56789.625, -67890.75};

  monitor.render(small);
  monitor.render(large);
  const auto writes = terminal.writes();
  ASSERT_EQ(writes.size(), 2U);

  const auto small_raw = line_starting_with(writes[0], "Raw");
  const auto large_raw = line_starting_with(writes[1], "Raw");
  const auto small_converted = line_starting_with(writes[0], "Converted");
  const auto large_converted = line_starting_with(writes[1], "Converted");
  ASSERT_FALSE(small_raw.empty());
  ASSERT_FALSE(small_converted.empty());
  EXPECT_EQ(small_raw.size(), large_raw.size());
  EXPECT_EQ(small_converted.size(), large_converted.size());
}

TEST(TerminalMonitor, ShorterFrameOverwritesEveryCharacterOfLongerFrame) {
  test::FakeTerminal terminal(TerminalCapabilities{100, 24, true});
  TerminalMonitor monitor(terminal);
  auto large = test::sample_record();
  large.host = "sensor-with-a-very-long-host-name";
  large.state = "streaming-with-an-unusually-long-state";
  large.receive_rate_hz = 9'999'999.99;
  large.rdt_sequence = 4'000'000'001U;
  large.ft_sequence = 4'000'000'000U;
  large.status = 4'000'000'002U;
  large.lost_count = std::numeric_limits<std::uint64_t>::max();
  large.duplicate_count = std::numeric_limits<std::uint64_t>::max() - 1;
  large.out_of_order_count = std::numeric_limits<std::uint64_t>::max() - 2;
  large.elapsed_seconds = 999'999.99999;

  monitor.render(large);
  const auto large_frame = terminal.screen_text();
  for (const auto value : {"host=sensor-with-a-very-long-host-name",
                           "state=streaming-with-an-unusually-long-state",
                           "rate_hz=",
                           "9999999.99",
                           "rdt=4000000001",
                           "ft=4000000000",
                           "status=4000000002",
                           "lost=18446744073709551615",
                           "duplicate=18446744073709551614",
                           "out_of_order=18446744073709551613",
                           "Elapsed",
                           "999999.99999",
                           "Raw",
                           "10",
                           "-60",
                           "Converted",
                           "1.25000",
                           "-6.75000",
                           "Units",
                           "N-mm"}) {
    EXPECT_NE(large_frame.find(value), std::string::npos) << value;
  }

  monitor.render(test::sample_record());

  const auto writes = terminal.writes();
  ASSERT_EQ(writes.size(), 2U);
  const auto second_frame = lines(writes.back());
  ASSERT_EQ(terminal.screen_rows().size(), second_frame.size());
  for (std::size_t row = 0; row < second_frame.size(); ++row) {
    EXPECT_EQ(terminal.screen_rows()[row], second_frame[row]) << "row " << row;
  }
}

TEST(TerminalMonitor, IncludesMeasurementsUnitsAndConnectionHealth) {
  test::FakeTerminal terminal(TerminalCapabilities{100, 24, true});
  TerminalMonitor monitor(terminal);
  monitor.render(test::sample_record());
  const auto writes = terminal.writes();
  ASSERT_EQ(writes.size(), 1U);
  const auto &frame = writes.front();

  for (const auto value : {"10", "-20", "30", "-40", "50", "-60", "1.25000", "-2.50000", "3.75000",
                           "-4.50000", "5.25000", "-6.75000"}) {
    EXPECT_NE(frame.find(value), std::string::npos) << value;
  }
  for (const auto value : {"N", "N-mm", "streaming", "7000.50", "lost=3", "duplicate=4",
                           "out_of_order=5", "rdt=41", "ft=40"}) {
    EXPECT_NE(frame.find(value), std::string::npos) << value;
  }
}

TEST(TerminalMonitor, NonAnsiOutputHasNoCursorOperationsOrEscapeBytes) {
  test::FakeTerminal terminal(TerminalCapabilities{80, 24, false});
  TerminalMonitor monitor(terminal);
  monitor.render(test::sample_record());
  monitor.render(test::next_sample_record());
  const auto writes = terminal.writes();

  EXPECT_EQ(terminal.clear_frame_count(), 0U);
  EXPECT_EQ(terminal.home_count(), 0U);
  ASSERT_EQ(writes.size(), 2U);
  for (const auto &text : writes) {
    EXPECT_EQ(text.find('\x1b'), std::string::npos);
    EXPECT_EQ(text.back(), '\n');
  }
}

TEST(HumanRendering, ExternalTextCannotInjectTerminalControls) {
  auto unsafe_sample = test::sample_record();
  unsafe_sample.host = "sensor\x1b[2J\nsecond-row";
  unsafe_sample.state = "streaming\rspoofed\tstate";
  unsafe_sample.force_unit = "N\x1b]0;owned\a";
  unsafe_sample.torque_unit = "N-mm\b\x7f";

  test::FakeTerminal ansi_terminal(TerminalCapabilities{100, 24, true});
  TerminalMonitor ansi_monitor(ansi_terminal);
  ansi_monitor.render(unsafe_sample);

  test::FakeTerminal compact_terminal(TerminalCapabilities{80, 24, false});
  TerminalMonitor compact_monitor(compact_terminal);
  compact_monitor.render(unsafe_sample);

  auto configuration = make_configuration_record(test::connection_options(), test::configuration());
  configuration.host = unsafe_sample.host;
  configuration.product_name = "product\x1b[31mred";
  configuration.force_unit = unsafe_sample.force_unit;
  configuration.torque_unit = unsafe_sample.torque_unit;
  configuration.calibration_source = "sensor\nforged";
  const BiasRecord bias{configuration, unsafe_sample, unsafe_sample};

  const auto ansi_writes = ansi_terminal.writes();
  const auto compact_writes = compact_terminal.writes();
  ASSERT_EQ(ansi_writes.size(), 1U);
  ASSERT_EQ(compact_writes.size(), 1U);
  for (const auto &text : {ansi_writes.front(), compact_writes.front(),
                           render_configuration_text(configuration), render_bias_text(bias)}) {
    EXPECT_FALSE(contains_unsafe_terminal_control(text));
  }
}

TEST(TerminalMonitor, CloseLeavesAnsiCursorOnAFreshLineAndFlushesOnce) {
  test::FakeTerminal terminal(TerminalCapabilities{100, 24, true});
  TerminalMonitor monitor(terminal);
  monitor.render(test::sample_record());
  const auto flushes_after_render = terminal.flush_count();

  monitor.close();
  monitor.close();

  const auto writes = terminal.writes();
  ASSERT_EQ(writes.size(), 2U);
  EXPECT_EQ(writes.back(), "\n");
  EXPECT_EQ(terminal.flush_count(), flushes_after_render + 1U);
}

TEST(TerminalText, RendersConfigurationAndBiasRecordsWithoutStableProseAssumptions) {
  const auto configuration =
      make_configuration_record(test::connection_options(), test::configuration());
  const BiasRecord bias{configuration, test::sample_record(41), test::sample_record(42)};

  const auto configuration_text = render_configuration_text(configuration);
  EXPECT_NE(configuration_text.find(configuration.host), std::string::npos);
  EXPECT_NE(configuration_text.find(configuration.product_name), std::string::npos);
  EXPECT_NE(configuration_text.find(configuration.force_unit), std::string::npos);
  EXPECT_NE(configuration_text.find(configuration.torque_unit), std::string::npos);

  const auto bias_text = render_bias_text(bias);
  EXPECT_NE(bias_text.find("41"), std::string::npos);
  EXPECT_NE(bias_text.find("42"), std::string::npos);
  EXPECT_NE(bias_text.find("1.25000"), std::string::npos);
  EXPECT_NE(bias_text.find("-6.75000"), std::string::npos);
}

} // namespace
} // namespace netft_cli
