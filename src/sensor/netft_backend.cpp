#include "sensor/netft_backend.hpp"

#include <netft/client.hpp>
#include <netft/discovery.hpp>

#include <memory>
#include <mutex>
#include <utility>

namespace netft_cli {
namespace {

netft::DiscoveryOptions discovery_options(const ConnectionOptions &options) {
  netft::DiscoveryOptions result;
  result.sensor_host = options.host;
  result.http_port = options.http_port;
  result.connect_timeout = options.timeout;
  result.total_timeout = options.timeout;
  return result;
}

netft::Config client_config(const ConnectionOptions &options) {
  netft::Config result;
  result.sensor_host = options.host;
  result.http_port = options.http_port;
  result.rdt_port = options.rdt_port;
  result.receive_timeout = options.timeout;
  result.configuration_connect_timeout = options.timeout;
  result.configuration_timeout = options.timeout;
  result.calibration_override.reset();
  return result;
}

class NetftSession final : public SensorSession {
public:
  explicit NetftSession(netft::Config config) : client_(std::move(config)) {}

  void start(Callback callback) override {
    client_.start([this, callback = std::move(callback)](const netft::Sample &sample) {
      std::scoped_lock lock(callback_gate_);
      callback(sample);
    });
  }
  void stop() noexcept override { client_.stop(); }
  void bias(BiasCompletion on_command_complete) override {
    std::scoped_lock lock(callback_gate_);
    on_command_complete(client_.bias());
  }
  netft::HealthSnapshot health() const override { return client_.health(); }

private:
  std::mutex callback_gate_;
  netft::Client client_;
};

} // namespace

netft::SensorConfiguration NetftBackend::discover(const ConnectionOptions &options) {
  return netft::discover_sensor(discovery_options(options));
}

std::unique_ptr<SensorSession> NetftBackend::open(const ConnectionOptions &options) {
  return std::make_unique<NetftSession>(client_config(options));
}

} // namespace netft_cli
