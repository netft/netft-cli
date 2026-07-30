#pragma once

#include "recording/writer.hpp"

#include <iosfwd>

namespace netft_cli {

class CsvRecordingWriter final : public RecordingWriter {
public:
  explicit CsvRecordingWriter(std::ostream &stream) noexcept : stream_(stream) {}
  void write(const RecordingRecord &record) override;

private:
  std::ostream &stream_;
  bool header_written_{};
};

} // namespace netft_cli
