#include "recording/output_file.hpp"

#include "app/error.hpp"

#include <exception>
#include <utility>

namespace netft_cli {

OutputFile::OutputFile(std::filesystem::path destination, Filesystem &filesystem)
    : destination_(std::move(destination)), partial_(destination_.string() + ".partial"),
      filesystem_(filesystem) {
  try {
    if (destination_.empty() || filesystem_.exists(destination_)) {
      throw AppError{ExitCode::Io, "recording destination already exists or is invalid"};
    }
    if (filesystem_.exists(partial_)) {
      throw AppError{ExitCode::Io, "recording partial file already exists"};
    }
    stream_ = std::make_unique<ExclusiveOutputFile>(partial_);
  } catch (const AppError &) {
    throw;
  } catch (const std::exception &) {
    throw AppError{ExitCode::Io, "recording output could not be prepared"};
  }
}

OutputFile::~OutputFile() = default;

void OutputFile::finalize() {
  if (finalized_) {
    return;
  }
  try {
    stream_->close();
    filesystem_.rename(partial_, destination_);
  } catch (const std::exception &) {
    throw AppError{ExitCode::Io, "recording output could not be finalized"};
  }
  finalized_ = true;
}

} // namespace netft_cli
