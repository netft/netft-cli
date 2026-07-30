#pragma once

#include "cli/options.hpp"
#include "config/environment.hpp"
#include "output/confirmation.hpp"
#include "output/context.hpp"
#include "platform/clock.hpp"
#include "platform/filesystem.hpp"
#include "platform/interrupt.hpp"
#include "sensor/backend.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace netft_cli {

class AppEnvironment {
public:
  virtual ~AppEnvironment() = default;

  virtual SensorBackend &backend() = 0;
  virtual OutputContext &output() = 0;
  virtual Confirmation &confirmation() = 0;
  virtual InterruptFlag &interrupt() = 0;
  virtual Clock &clock() = 0;
  virtual WallClock &wall_clock() = 0;
  virtual Filesystem &filesystem() = 0;
  [[nodiscard]] virtual EnvironmentMap environment() const { return {}; }
  virtual int show_help(const ShowHelp &help) = 0;
  virtual int show_version() = 0;
};

class NativeEnvironment final : public AppEnvironment {
public:
  NativeEnvironment();
  ~NativeEnvironment() override;

  NativeEnvironment(const NativeEnvironment &) = delete;
  NativeEnvironment &operator=(const NativeEnvironment &) = delete;
  NativeEnvironment(NativeEnvironment &&) = delete;
  NativeEnvironment &operator=(NativeEnvironment &&) = delete;

  SensorBackend &backend() override;
  OutputContext &output() override;
  Confirmation &confirmation() override;
  InterruptFlag &interrupt() override;
  Clock &clock() override;
  WallClock &wall_clock() override;
  Filesystem &filesystem() override;
  [[nodiscard]] EnvironmentMap environment() const override;
  int show_help(const ShowHelp &help) override;
  int show_version() override;

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

int run_application(const std::vector<std::string_view> &arguments, AppEnvironment &environment);

} // namespace netft_cli
