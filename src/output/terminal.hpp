#pragma once

#include "output/records.hpp"
#include "platform/terminal.hpp"

#include <string>

namespace netft_cli {

std::string render_configuration_text(const ConfigurationRecord &record);
std::string render_bias_text(const BiasRecord &record);

class TerminalMonitor {
public:
  explicit TerminalMonitor(TerminalWriter &terminal) noexcept;
  ~TerminalMonitor();

  TerminalMonitor(const TerminalMonitor &) = delete;
  TerminalMonitor &operator=(const TerminalMonitor &) = delete;

  void render(const SampleRecord &record);
  void close();

private:
  TerminalWriter &terminal_;
  bool frame_started_{false};
  bool closed_{false};
};

} // namespace netft_cli
