#pragma once

#include "sensor/backend.hpp"

#include <netft/discovery.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace netft_cli::test {

class FakeSession final : public SensorSession {
public:
  explicit FakeSession(bool biased_start = false) : biased_start_(biased_start) {}
  void start(Callback callback) override;
  void stop() noexcept override;
  void stop_and_hold_port() noexcept override;
  netft::HealthSnapshot health() const override;

  void set_samples(std::vector<netft::Sample> samples) { samples_ = std::move(samples); }
  void set_held_backlog(std::vector<netft::Sample> samples) { held_backlog_ = std::move(samples); }
  void set_health(netft::HealthSnapshot health) { health_ = std::move(health); }
  void fail_start(bool fail = true) noexcept { fail_start_ = fail; }
  void fail_start_after_bias(bool fail = true) noexcept { fail_start_after_bias_ = fail; }
  void set_before_first_sample(std::function<void()> hook) {
    before_first_sample_ = std::move(hook);
  }

  std::size_t start_calls() const noexcept { return start_calls_; }
  std::size_t stop_calls() const noexcept { return stop_calls_; }
  std::size_t stop_and_hold_port_calls() const noexcept { return stop_and_hold_port_calls_; }
  std::size_t held_backlog_count() const noexcept { return held_backlog_.size(); }
  std::size_t bias_send_calls() const noexcept { return bias_send_calls_; }
  std::size_t start_send_calls() const noexcept { return start_send_calls_; }
  std::size_t receive_calls() const noexcept { return receive_calls_; }
  const std::vector<std::string> &startup_events() const noexcept { return startup_events_; }

private:
  std::vector<netft::Sample> samples_;
  std::vector<netft::Sample> held_backlog_;
  std::vector<std::string> startup_events_;
  std::function<void()> before_first_sample_;
  Callback callback_;
  netft::HealthSnapshot health_;
  std::size_t start_calls_{};
  std::size_t stop_calls_{};
  std::size_t stop_and_hold_port_calls_{};
  std::size_t bias_send_calls_{};
  std::size_t start_send_calls_{};
  std::size_t receive_calls_{};
  bool biased_start_{};
  bool fail_start_{};
  bool fail_start_after_bias_{};
};

class FakeBackend final : public SensorBackend {
public:
  FakeBackend();

  netft::SensorConfiguration discover(const ConnectionOptions &options) override;
  std::unique_ptr<SensorSession> open(const ConnectionOptions &options) override;
  std::unique_ptr<SensorSession> open_biased(const ConnectionOptions &options) override;

  FakeSession &session() noexcept { return *session_; }
  const FakeSession &session() const noexcept { return *session_; }
  FakeSession &biased_session() noexcept { return *biased_session_; }
  const FakeSession &biased_session() const noexcept { return *biased_session_; }
  void set_configuration(netft::SensorConfiguration configuration) {
    configuration_ = std::move(configuration);
  }
  void fail_discovery(bool fail = true) noexcept { fail_discovery_ = fail; }
  void fail_open(bool fail = true) noexcept { fail_open_ = fail; }

  std::size_t discover_calls() const noexcept { return discover_options_.size(); }
  std::size_t open_calls() const noexcept { return open_options_.size(); }
  bool preview_alive_at_biased_connect() const noexcept { return preview_alive_at_biased_connect_; }
  bool preview_alive_at_biased_first_sample() const noexcept {
    return preview_alive_at_biased_first_sample_;
  }
  const std::vector<ConnectionOptions> &discover_options() const noexcept {
    return discover_options_;
  }
  const std::vector<ConnectionOptions> &open_options() const noexcept { return open_options_; }

private:
  std::shared_ptr<FakeSession> session_;
  std::shared_ptr<FakeSession> biased_session_;
  std::shared_ptr<bool> preview_alive_;
  netft::SensorConfiguration configuration_;
  std::vector<ConnectionOptions> discover_options_;
  std::vector<ConnectionOptions> open_options_;
  bool fail_discovery_{};
  bool fail_open_{};
  bool preview_alive_at_biased_connect_{};
  bool preview_alive_at_biased_first_sample_{};
};

} // namespace netft_cli::test
