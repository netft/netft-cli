#pragma once

#include "output/context.hpp"
#include "output/records.hpp"

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
  explicit TerminalConfirmation(OutputContext &output) noexcept : output_(output) {}

  bool confirm(const BiasPreview &preview) override;

private:
  OutputContext &output_;
};

} // namespace netft_cli
