#pragma once

#include "platform/interrupt.hpp"

#include <memory>
#include <string>

namespace netft_cli {

enum class LineReadStatus { Line, Eof, Interrupted, Error };

struct LineReadResult {
  LineReadStatus status;
  std::string line;
};

class InterruptibleLineReader {
public:
  virtual ~InterruptibleLineReader() = default;
  virtual LineReadResult read_line(const InterruptFlag &interrupt) = 0;
};

std::unique_ptr<InterruptibleLineReader> make_interruptible_line_reader(int file_descriptor);

} // namespace netft_cli
