#include "platform/filesystem.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <streambuf>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace netft_cli {
namespace {

class FileBuffer final : public std::streambuf {
public:
  explicit FileBuffer(const std::filesystem::path &path) {
#ifdef _WIN32
    const auto descriptor =
        _wopen(path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    const auto descriptor = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
#endif
    if (descriptor < 0)
      throw std::system_error(errno, std::generic_category(), "create recording");
#ifdef _WIN32
    file_ = _wfdopen(descriptor, L"wb");
#else
    file_ = ::fdopen(descriptor, "wb");
#endif
    if (!file_) {
      const auto error = errno;
#ifdef _WIN32
      _close(descriptor);
#else
      ::close(descriptor);
#endif
      throw std::system_error(error, std::generic_category(), "open recording stream");
    }
    setp(buffer_.data(), buffer_.data() + buffer_.size());
  }
  ~FileBuffer() override { static_cast<void>(close()); }
  bool close() noexcept {
    if (!file_)
      return true;
    const bool flushed = sync() == 0;
    const bool closed = std::fclose(file_) == 0;
    file_ = nullptr;
    setp(nullptr, nullptr);
    return flushed && closed;
  }

protected:
  int sync() override {
    if (!file_)
      return -1;
    const auto count = static_cast<std::size_t>(pptr() - pbase());
    if (count != 0 && std::fwrite(pbase(), 1, count, file_) != count)
      return -1;
    setp(buffer_.data(), buffer_.data() + buffer_.size());
    return std::fflush(file_);
  }
  int_type overflow(int_type value) override {
    if (sync() != 0)
      return traits_type::eof();
    if (!traits_type::eq_int_type(value, traits_type::eof())) {
      *pptr() = traits_type::to_char_type(value);
      pbump(1);
    }
    return traits_type::not_eof(value);
  }
  std::streamsize xsputn(const char *source, std::streamsize count) override {
    std::streamsize written{};
    while (written < count && file_) {
      if (pptr() == epptr() && sync() != 0)
        break;
      const auto chunk = std::min(count - written, static_cast<std::streamsize>(epptr() - pptr()));
      std::memcpy(pptr(), source + written, static_cast<std::size_t>(chunk));
      pbump(static_cast<int>(chunk));
      written += chunk;
    }
    return written;
  }

private:
  std::FILE *file_{};
  std::array<char, 16384> buffer_{};
};

} // namespace

struct ExclusiveOutputFile::Impl {
  explicit Impl(const std::filesystem::path &path) : buffer(path), stream(&buffer) {}
  FileBuffer buffer;
  std::ostream stream;
};
ExclusiveOutputFile::ExclusiveOutputFile(const std::filesystem::path &path)
    : impl_(std::make_unique<Impl>(path)) {}
ExclusiveOutputFile::~ExclusiveOutputFile() = default;
std::ostream &ExclusiveOutputFile::stream() noexcept { return impl_->stream; }
void ExclusiveOutputFile::close() {
  impl_->stream.flush();
  const bool closed = impl_->buffer.close();
  if (!impl_->stream || !closed)
    throw std::ios_base::failure("recording close failed");
}

bool NativeFilesystem::exists(const std::filesystem::path &path) const {
  // symlink_status also observes a dangling link; exists(path) follows it.
  return std::filesystem::exists(std::filesystem::symlink_status(path));
}

void NativeFilesystem::rename(const std::filesystem::path &source,
                              const std::filesystem::path &destination) {
#ifdef _WIN32
  if (!MoveFileW(source.c_str(), destination.c_str())) {
    throw std::filesystem::filesystem_error(
        "finalize recording", source, destination,
        std::error_code(static_cast<int>(GetLastError()), std::system_category()));
  }
#else
  // Same-directory hard-link publication atomically refuses every existing
  // destination, including dangling links. No check-then-rename race.
  if (::link(source.c_str(), destination.c_str()) != 0) {
    throw std::filesystem::filesystem_error("publish recording", source, destination,
                                            std::error_code(errno, std::generic_category()));
  }
  if (::unlink(source.c_str()) != 0) {
    throw std::filesystem::filesystem_error("remove recording partial", source,
                                            std::error_code(errno, std::generic_category()));
  }
#endif
}

} // namespace netft_cli
