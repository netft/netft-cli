#pragma once

#include "output/context.hpp"
#include "output/records.hpp"
#include "platform/line_reader.hpp"

namespace netft_cli {

struct BiasPreview {
  ConfigurationRecord configuration;
  SampleRecord sample;
};

class Confirmation {
public:
  virtual ~Confirmation() = default;
  virtual bool confirm(const BiasPreview &preview) = 0;
};

class TerminalConfirmation final : public Confirmation {
public:
  TerminalConfirmation(OutputContext &output, InterruptFlag &interrupt,
                       InterruptibleLineReader &line_reader) noexcept
      : output_(output), interrupt_(interrupt), line_reader_(line_reader) {}

  bool confirm(const BiasPreview &preview) override;

private:
  OutputContext &output_;
  InterruptFlag &interrupt_;
  InterruptibleLineReader &line_reader_;
};

} // namespace netft_cli
