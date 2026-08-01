#include <netft/client.hpp>

#include <gtest/gtest.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

namespace {

using namespace std::chrono_literals;

class LoopbackSensor {
public:
  LoopbackSensor() {
    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
      throw std::runtime_error("socket failed");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(socket_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
      throw std::runtime_error("bind failed");
    }
    socklen_t size = sizeof(address);
    if (::getsockname(socket_, reinterpret_cast<sockaddr *>(&address), &size) != 0) {
      throw std::runtime_error("getsockname failed");
    }
    port_ = ntohs(address.sin_port);
    timeval timeout{0, 300'000};
    static_cast<void>(::setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
  }

  ~LoopbackSensor() { ::close(socket_); }

  int port() const noexcept { return port_; }

  std::uint16_t receive_command(sockaddr_in &peer) {
    std::array<std::uint8_t, 8> bytes{};
    socklen_t size = sizeof(peer);
    const auto received = ::recvfrom(socket_, bytes.data(), bytes.size(), 0,
                                     reinterpret_cast<sockaddr *>(&peer), &size);
    if (received != 8 || bytes[0] != 0x12 || bytes[1] != 0x34) {
      throw std::runtime_error("request receive failed");
    }
    return static_cast<std::uint16_t>((bytes[2] << 8U) | bytes[3]);
  }

  void send_record(const sockaddr_in &peer, const std::uint32_t sequence) {
    std::array<std::uint8_t, 36> record{};
    put_u32(record, 0, sequence);
    put_u32(record, 4, sequence);
    static_cast<void>(::sendto(socket_, record.data(), record.size(), 0,
                               reinterpret_cast<const sockaddr *>(&peer), sizeof(peer)));
  }

private:
  static void put_u32(std::array<std::uint8_t, 36> &bytes, const std::size_t offset,
                      const std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) {
      bytes[offset + index] = static_cast<std::uint8_t>(value >> (24U - 8U * index));
    }
  }

  int socket_{-1};
  int port_{};
};

netft::Config config_for(const LoopbackSensor &sensor) {
  netft::Config config;
  config.sensor_host = "127.0.0.1";
  config.rdt_port = sensor.port();
  config.receive_timeout = 100ms;
  config.recovery_policy = netft::RecoveryPolicy::FailStop;
  config.calibration_override =
      netft::Calibration{1.0, 1.0, netft::ForceUnit::Newton, netft::TorqueUnit::NewtonMeter};
  return config;
}

TEST(ClientSocketIsolation, RetainedPreviewPortAndBacklogStaySeparateFromBiasedClient) {
  LoopbackSensor sensor;
  auto preview_config = config_for(sensor);
  preview_config.retain_bound_socket_until_destruction = true;
  netft::Client preview{preview_config};
  preview.start([](const netft::Sample &) {});
  sockaddr_in preview_peer{};
  ASSERT_EQ(sensor.receive_command(preview_peer), 0x0002);
  sensor.send_record(preview_peer, 1);
  ASSERT_TRUE(preview.wait_for_first_sample(500ms));
  preview.stop();
  ASSERT_EQ(sensor.receive_command(preview_peer), 0x0000);
  sensor.send_record(preview_peer, 99);

  auto biased_config = config_for(sensor);
  biased_config.startup_mode = netft::StartupMode::BiasAndStream;
  netft::Client biased{biased_config};
  std::atomic<std::uint32_t> delivered{};
  biased.start([&](const netft::Sample &sample) { delivered = sample.rdt_sequence; });
  sockaddr_in biased_peer{};
  ASSERT_EQ(sensor.receive_command(biased_peer), 0x0042);
  ASSERT_EQ(sensor.receive_command(biased_peer), 0x0002);
  EXPECT_NE(preview_peer.sin_port, biased_peer.sin_port);
  sensor.send_record(biased_peer, 2);
  ASSERT_TRUE(biased.wait_for_first_sample(500ms));
  EXPECT_EQ(delivered.load(), 2U);
  biased.stop();
}

TEST(ClientSocketIsolation, BiasedFailStopTimeoutDoesNotSendBiasAgain) {
  LoopbackSensor sensor;
  auto config = config_for(sensor);
  config.startup_mode = netft::StartupMode::BiasAndStream;
  netft::Client client{config};
  client.start([](const netft::Sample &) {});
  sockaddr_in peer{};
  ASSERT_EQ(sensor.receive_command(peer), 0x0042);
  ASSERT_EQ(sensor.receive_command(peer), 0x0002);
  std::this_thread::sleep_for(200ms);
  EXPECT_EQ(client.health().state, netft::ClientState::Faulted);
  EXPECT_EQ(sensor.receive_command(peer), 0x0000);
  EXPECT_THROW(static_cast<void>(sensor.receive_command(peer)), std::runtime_error);
  client.stop();
}

} // namespace
#endif
