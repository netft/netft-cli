#pragma once

#include <filesystem>

namespace netft_cli {

class Filesystem {
public:
  virtual ~Filesystem() = default;
  [[nodiscard]] virtual bool exists(const std::filesystem::path &path) const = 0;
  virtual void rename(const std::filesystem::path &source,
                      const std::filesystem::path &destination) = 0;
};

class NativeFilesystem final : public Filesystem {
public:
  [[nodiscard]] bool exists(const std::filesystem::path &path) const override;
  void rename(const std::filesystem::path &source,
              const std::filesystem::path &destination) override;
};

} // namespace netft_cli
