#pragma once

#include <filesystem>
#include <memory>
#include <ostream>

namespace netft_cli {

// Owns the exclusively created file for its complete write lifetime.
class ExclusiveOutputFile {
public:
  explicit ExclusiveOutputFile(const std::filesystem::path &path);
  ~ExclusiveOutputFile();
  ExclusiveOutputFile(const ExclusiveOutputFile &) = delete;
  ExclusiveOutputFile &operator=(const ExclusiveOutputFile &) = delete;
  [[nodiscard]] std::ostream &stream() noexcept;
  void close();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

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
