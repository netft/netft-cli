#include "support/fake_backend.hpp"

namespace netft_cli::test {
namespace {

class SharedSession final : public SensorSession {
public:
  explicit SharedSession(std::shared_ptr<FakeSession> session) : session_(std::move(session)) {}

  void start(Callback callback) override { session_->start(std::move(callback)); }
  void stop() noexcept override { session_->stop(); }
  void bias(BiasCompletion on_command_complete) override {
    session_->bias(std::move(on_command_complete));
  }
  netft::HealthSnapshot health() const override { return session_->health(); }

private:
  std::shared_ptr<FakeSession> session_;
};

} // namespace

void FakeSession::start(Callback callback) {
  ++start_calls_;
  if (fail_start_) {
    throw std::runtime_error("fake start failure");
  }
  callback_ = std::move(callback);
  for (const auto &sample : samples_) {
    callback_(sample);
  }
}

void FakeSession::stop() noexcept {
  ++stop_calls_;
  callback_ = {};
}

void FakeSession::bias(BiasCompletion on_command_complete) {
  ++bias_calls_;
  if (fail_bias_) {
    throw std::runtime_error("fake bias failure");
  }
  for (const auto &sample : during_bias_samples_) {
    callback_(sample);
  }
  on_command_complete();
  for (const auto &sample : completion_boundary_samples_) {
    callback_(sample);
  }
  for (const auto &sample : post_bias_samples_) {
    callback_(sample);
  }
}

netft::HealthSnapshot FakeSession::health() const { return health_; }

FakeBackend::FakeBackend() : session_(std::make_shared<FakeSession>()) {}

netft::SensorConfiguration FakeBackend::discover(const ConnectionOptions &options) {
  discover_options_.push_back(options);
  if (fail_discovery_) {
    throw netft::DiscoveryError("fake discovery failure");
  }
  return configuration_;
}

std::unique_ptr<SensorSession> FakeBackend::open(const ConnectionOptions &options) {
  open_options_.push_back(options);
  if (fail_open_) {
    throw std::runtime_error("fake open failure");
  }
  return std::make_unique<SharedSession>(session_);
}

} // namespace netft_cli::test
