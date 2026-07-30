#pragma once

#include "output/context.hpp"

#include <sstream>
#include <string>
#include <utility>

namespace netft_cli::test {

class MemoryOutput {
public:
  explicit MemoryOutput(bool terminal) : MemoryOutput("", terminal, terminal) {}

  MemoryOutput(std::string input, bool input_is_terminal, bool output_is_terminal)
      : input_(std::move(input)),
        context_{input_, standard_output_, standard_error_, input_is_terminal, output_is_terminal,
                 {}} {}

  OutputContext &context() noexcept { return context_; }
  std::string standard_output_text() const { return standard_output_.str(); }
  std::string standard_error_text() const { return standard_error_.str(); }

private:
  std::istringstream input_;
  std::ostringstream standard_output_;
  std::ostringstream standard_error_;
  OutputContext context_;
};

} // namespace netft_cli::test
