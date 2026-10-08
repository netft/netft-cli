#pragma once

#include "platform/filesystem.hpp"

#include <filesystem>
#include <memory>

namespace netft_cli {

class OutputFile {
public:
  OutputFile(std::filesystem::path destination, Filesystem &filesystem);
  ~OutputFile();

  OutputFile(const OutputFile &) = delete;
  OutputFile &operator=(const OutputFile &) = delete;
  OutputFile(OutputFile &&) = delete;
  OutputFile &operator=(OutputFile &&) = delete;

  [[nodiscard]] std::ostream &stream() noexcept { return stream_->stream(); }
  void finalize();
  [[nodiscard]] bool finalized() const noexcept { return finalized_; }
  [[nodiscard]] const std::filesystem::path &partial_path() const noexcept { return partial_; }

private:
  std::filesystem::path destination_;
  std::filesystem::path partial_;
  Filesystem &filesystem_;
  std::unique_ptr<ExclusiveOutputFile> stream_;
  bool finalized_{};
};

} // namespace netft_cli
