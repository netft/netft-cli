#pragma once

#include "cli/options.hpp"
#include "sensor/backend.hpp"

#include <memory>

namespace netft_cli {

class Acquisition {
public:
  [[nodiscard]] static Acquisition open(SensorBackend &backend,
                                        const ConnectionOptions &connection);

  explicit Acquisition(std::unique_ptr<SensorSession> session);
  ~Acquisition();

  Acquisition(const Acquisition &) = delete;
  Acquisition &operator=(const Acquisition &) = delete;
  Acquisition(Acquisition &&) = delete;
  Acquisition &operator=(Acquisition &&) = delete;

  void start(SensorSession::Callback callback);
  void stop() noexcept;
  [[nodiscard]] netft::HealthSnapshot health() const;

private:
  std::unique_ptr<SensorSession> session_;
  bool active_{};
};

} // namespace netft_cli
