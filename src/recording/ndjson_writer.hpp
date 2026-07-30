#pragma once

#include "recording/writer.hpp"

#include <iosfwd>

namespace netft_cli {

class NdjsonRecordingWriter final : public RecordingWriter {
public:
  explicit NdjsonRecordingWriter(std::ostream &stream) noexcept : stream_(stream) {}
  void write(const RecordingRecord &record) override;

private:
  std::ostream &stream_;
};

} // namespace netft_cli
