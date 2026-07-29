#pragma once

#include "cli/options.hpp"

#include <netft/types.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace netft_cli {

class SensorSession {
public:
  using Callback = std::function<void(const netft::Sample &)>;
  using AcquisitionEpoch = std::uint64_t;
  using BiasCompletion = std::function<void(AcquisitionEpoch)>;

  virtual ~SensorSession() = default;
  virtual void start(Callback callback) = 0;
  // Returns only after callback delivery is quiescent; no callback may begin after it returns.
  virtual void stop() noexcept = 0;
  // Serializes callback delivery with the sensor command, then invokes on_command_complete with
  // the new acquisition epoch after both command datagrams succeed and before any subsequent
  // callback can begin.
  virtual void bias(BiasCompletion on_command_complete) = 0;
  virtual netft::HealthSnapshot health() const = 0;
};

class SensorBackend {
public:
  virtual ~SensorBackend() = default;
  virtual netft::SensorConfiguration discover(const ConnectionOptions &options) = 0;
  virtual std::unique_ptr<SensorSession> open(const ConnectionOptions &options) = 0;
};

} // namespace netft_cli
