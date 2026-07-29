#pragma once

#include "output/records.hpp"

#include <iosfwd>

namespace netft_cli {

class CsvWriter {
public:
  explicit CsvWriter(std::ostream &stream) noexcept : stream_(stream) {}

  void write(const SampleRecord &record);

private:
  std::ostream &stream_;
  bool header_written_{};
};

} // namespace netft_cli
