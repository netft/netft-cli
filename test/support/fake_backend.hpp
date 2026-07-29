#pragma once

#include "sensor/backend.hpp"

#include <netft/discovery.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace netft_cli::test {

class FakeSession final : public SensorSession {
public:
  void start(Callback callback) override;
  void stop() noexcept override;
  void bias() override;
  netft::HealthSnapshot health() const override;

  void set_samples(std::vector<netft::Sample> samples) { samples_ = std::move(samples); }
  void set_health(netft::HealthSnapshot health) { health_ = std::move(health); }
  void fail_start(bool fail = true) noexcept { fail_start_ = fail; }
  void fail_bias(bool fail = true) noexcept { fail_bias_ = fail; }

  std::size_t start_calls() const noexcept { return start_calls_; }
  std::size_t stop_calls() const noexcept { return stop_calls_; }
  std::size_t bias_calls() const noexcept { return bias_calls_; }

private:
  std::vector<netft::Sample> samples_;
  netft::HealthSnapshot health_;
  std::size_t start_calls_{};
  std::size_t stop_calls_{};
  std::size_t bias_calls_{};
  bool fail_start_{};
  bool fail_bias_{};
};

class FakeBackend final : public SensorBackend {
public:
  FakeBackend();

  netft::SensorConfiguration discover(const ConnectionOptions &options) override;
  std::unique_ptr<SensorSession> open(const ConnectionOptions &options) override;

  FakeSession &session() noexcept { return *session_; }
  const FakeSession &session() const noexcept { return *session_; }
  void set_configuration(netft::SensorConfiguration configuration) {
    configuration_ = std::move(configuration);
  }
  void fail_discovery(bool fail = true) noexcept { fail_discovery_ = fail; }
  void fail_open(bool fail = true) noexcept { fail_open_ = fail; }

  std::size_t discover_calls() const noexcept { return discover_options_.size(); }
  std::size_t open_calls() const noexcept { return open_options_.size(); }
  const std::vector<ConnectionOptions> &discover_options() const noexcept {
    return discover_options_;
  }
  const std::vector<ConnectionOptions> &open_options() const noexcept { return open_options_; }

private:
  std::shared_ptr<FakeSession> session_;
  netft::SensorConfiguration configuration_;
  std::vector<ConnectionOptions> discover_options_;
  std::vector<ConnectionOptions> open_options_;
  bool fail_discovery_{};
  bool fail_open_{};
};

} // namespace netft_cli::test
