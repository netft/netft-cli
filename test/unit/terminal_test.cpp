#include "output/terminal.hpp"
#include "support/fake_terminal.hpp"
#include "support/options.hpp"
#include "support/records.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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
