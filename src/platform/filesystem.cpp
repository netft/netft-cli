#include "platform/filesystem.hpp"

namespace netft_cli {

bool NativeFilesystem::exists(const std::filesystem::path &path) const {
  return std::filesystem::exists(path);
}

void NativeFilesystem::rename(const std::filesystem::path &source,
                              const std::filesystem::path &destination) {
  if (std::filesystem::exists(destination)) {
    throw std::filesystem::filesystem_error("recording destination already exists", destination,
                                            std::make_error_code(std::errc::file_exists));
  }
  std::filesystem::rename(source, destination);
}

} // namespace netft_cli
