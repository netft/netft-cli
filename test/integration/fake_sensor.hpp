#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace netft_cli::test {

class FakeSensor {
public:
  explicit FakeSensor(double rate_hz = 200.0);
  ~FakeSensor();

  FakeSensor(const FakeSensor &) = delete;
  FakeSensor &operator=(const FakeSensor &) = delete;

  const std::string &host() const noexcept;
  int http_port() const noexcept;
  int rdt_port() const noexcept;

  void pause() noexcept;
  void resume() noexcept;
  void queue_record(std::uint32_t rdt_sequence, std::uint32_t status = 0,
                    std::uint32_t ft_sequence = 0,
                    std::array<std::int32_t, 6> axes = {100, -200, 300, 10, -20, 30});
  void skip_records(unsigned count) noexcept;
  void set_http_response(std::string xml, int status = 200);
  void set_http_response_delay(std::chrono::milliseconds delay);

  bool wait_for_start_realtime(unsigned count = 1,
                               std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                   1000}) const;
  bool wait_for_stop_streaming(unsigned count = 1,
                               std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                   1000}) const;
  unsigned start_realtime_count() const noexcept;
  unsigned stop_streaming_count() const noexcept;
  unsigned software_bias_count() const noexcept;
  std::uint64_t http_request_count() const noexcept;

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace netft_cli::test
