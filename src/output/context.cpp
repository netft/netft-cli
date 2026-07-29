#include "output/context.hpp"

#include "app/error.hpp"

#include <ios>
#include <memory>

namespace netft_cli {

OutputHandle::OutputHandle(std::unique_ptr<std::ofstream> stream) noexcept
    : owned_stream_(std::move(stream)), stream_(owned_stream_.get()) {}

OutputHandle OutputHandle::standard(std::ostream &stream) { return OutputHandle{stream}; }

OutputHandle OutputHandle::file(const std::filesystem::path &path) {
  auto stream =
      std::make_unique<std::ofstream>(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!stream->is_open()) {
    throw AppError{ExitCode::Io, "failed to open output file"};
  }
  return OutputHandle{std::move(stream)};
}

void OutputHandle::flush() {
  stream_->flush();
  if (!*stream_) {
    throw AppError{ExitCode::Io, "failed to flush output"};
  }
}

} // namespace netft_cli
