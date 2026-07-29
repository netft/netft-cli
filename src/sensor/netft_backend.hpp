#pragma once

#include "sensor/backend.hpp"

namespace netft_cli {

class NetftBackend final : public SensorBackend {
public:
  netft::SensorConfiguration discover(const ConnectionOptions &options) override;
  std::unique_ptr<SensorSession> open(const ConnectionOptions &options) override;
  std::unique_ptr<SensorSession> open_biased(const ConnectionOptions &options) override;
};

} // namespace netft_cli
