#include "support/fake_backend.hpp"

namespace netft_cli::test {
namespace {

class SharedSession final : public SensorSession {
public:
  SharedSession(std::shared_ptr<FakeSession> session, std::shared_ptr<bool> alive = {})
      : session_(std::move(session)), alive_(std::move(alive)) {
    if (alive_) {
      *alive_ = true;
    }
  }
  ~SharedSession() override {
    if (alive_) {
      *alive_ = false;
    }
  }

  void start(Callback callback) override { session_->start(std::move(callback)); }
  void stop() noexcept override { session_->stop(); }
  netft::HealthSnapshot health() const override { return session_->health(); }

private:
  std::shared_ptr<FakeSession> session_;
  std::shared_ptr<bool> alive_;
};

} // namespace

void FakeSession::start(Callback callback) {
  ++start_calls_;
  stopped_ = false;
  if (fail_start_) {
    throw std::runtime_error("fake start failure");
  }
  if (biased_start_) {
    startup_events_.push_back("bias");
    ++bias_send_calls_;
    startup_events_.push_back("start");
    ++start_send_calls_;
    if (fail_start_after_bias_) {
      throw std::runtime_error("fake start realtime failure");
    }
    startup_events_.push_back("receive");
    ++receive_calls_;
  }
  callback_ = std::move(callback);
  if (!samples_.empty() && before_first_sample_) {
    before_first_sample_();
  }
  for (std::size_t index = 0; index < samples_.size(); ++index) {
    callback_(samples_[index]);
    if (after_sample_) {
      after_sample_(index + 1);
    }
  }
}

void FakeSession::stop() noexcept {
  if (!stopped_) {
    ++stop_calls_;
    stopped_ = true;
  }
  callback_ = {};
}

netft::HealthSnapshot FakeSession::health() const { return health_; }

FakeBackend::FakeBackend()
    : session_(std::make_shared<FakeSession>()),
      biased_session_(std::make_shared<FakeSession>(true)),
      preview_alive_(std::make_shared<bool>(false)) {}

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

std::unique_ptr<SensorSession> FakeBackend::open_preview(const ConnectionOptions &options) {
  open_options_.push_back(options);
  if (fail_open_) {
    throw std::runtime_error("fake open failure");
  }
  return std::make_unique<SharedSession>(session_, preview_alive_);
}

std::unique_ptr<SensorSession> FakeBackend::open_biased(const ConnectionOptions &options) {
  open_options_.push_back(options);
  if (fail_open_) {
    throw std::runtime_error("fake open failure");
  }
  preview_alive_at_biased_connect_ = *preview_alive_;
  biased_session_->set_before_first_sample(
      [this] { preview_alive_at_biased_first_sample_ = *preview_alive_; });
  return std::make_unique<SharedSession>(biased_session_);
}

} // namespace netft_cli::test
