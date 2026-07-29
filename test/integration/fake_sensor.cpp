#include "integration/fake_sensor.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace netft_cli::test {
namespace {

constexpr std::string_view default_xml =
    "<netft><prodname>Loopback Net F/T</prodname><cfgcpf>1000000</cfgcpf>"
    "<cfgcpt>1000</cfgcpt><scfgfu>N</scfgfu><scfgtu>Nmm</scfgtu></netft>";

constexpr std::uint16_t stop_streaming = 0x0000;
constexpr std::uint16_t start_realtime = 0x0002;
constexpr std::uint16_t set_software_bias = 0x0042;

#ifdef _WIN32
using NativeSocket = SOCKET;
using SocketLength = int;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
using SocketLength = socklen_t;
constexpr NativeSocket invalid_socket = -1;
#endif

class SocketRuntime {
public:
  SocketRuntime() {
#ifdef _WIN32
    WSADATA data{};
    const int result = ::WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
      throw std::runtime_error("test WinSock startup failed");
    }
#endif
  }

  ~SocketRuntime() {
#ifdef _WIN32
    ::WSACleanup();
#endif
  }

  SocketRuntime(const SocketRuntime &) = delete;
  SocketRuntime &operator=(const SocketRuntime &) = delete;
};

bool valid(NativeSocket socket) noexcept { return socket != invalid_socket; }

void close_socket(NativeSocket &socket) noexcept {
  if (!valid(socket)) {
    return;
  }
#ifdef _WIN32
  static_cast<void>(::closesocket(socket));
#else
  static_cast<void>(::close(socket));
#endif
  socket = invalid_socket;
}

void shutdown_socket(NativeSocket socket) noexcept {
  if (!valid(socket)) {
    return;
  }
#ifdef _WIN32
  static_cast<void>(::shutdown(socket, SD_BOTH));
#else
  static_cast<void>(::shutdown(socket, SHUT_RDWR));
#endif
}

bool set_nonblocking(NativeSocket socket) noexcept {
#ifdef _WIN32
  u_long enabled = 1;
  return ::ioctlsocket(socket, FIONBIO, &enabled) != SOCKET_ERROR;
#else
  const int flags = ::fcntl(socket, F_GETFL, 0);
  return flags >= 0 && ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

std::size_t bounded_socket_size(std::size_t size) noexcept {
#ifdef _WIN32
  return std::min(size, static_cast<std::size_t>(std::numeric_limits<int>::max()));
#else
  return size;
#endif
}

std::ptrdiff_t receive_from(NativeSocket socket, void *data, std::size_t size, sockaddr *address,
                            SocketLength *address_size) noexcept {
  const auto bounded = bounded_socket_size(size);
#ifdef _WIN32
  return ::recvfrom(socket, static_cast<char *>(data), static_cast<int>(bounded), 0, address,
                    address_size);
#else
  return ::recvfrom(socket, data, bounded, 0, address, address_size);
#endif
}

std::ptrdiff_t send_to(NativeSocket socket, const void *data, std::size_t size,
                       const sockaddr *address, SocketLength address_size) noexcept {
  const auto bounded = bounded_socket_size(size);
#ifdef _WIN32
  return ::sendto(socket, static_cast<const char *>(data), static_cast<int>(bounded), 0, address,
                  address_size);
#else
  return ::sendto(socket, data, bounded, 0, address, address_size);
#endif
}

std::ptrdiff_t receive(NativeSocket socket, void *data, std::size_t size) noexcept {
  const auto bounded = bounded_socket_size(size);
#ifdef _WIN32
  return ::recv(socket, static_cast<char *>(data), static_cast<int>(bounded), 0);
#else
  return ::recv(socket, data, bounded, 0);
#endif
}

std::ptrdiff_t send_bytes(NativeSocket socket, const void *data, std::size_t size) noexcept {
  const auto bounded = bounded_socket_size(size);
#ifdef _WIN32
  return ::send(socket, static_cast<const char *>(data), static_cast<int>(bounded), 0);
#else
#ifdef MSG_NOSIGNAL
  return ::send(socket, data, bounded, MSG_NOSIGNAL);
#else
  return ::send(socket, data, bounded, 0);
#endif
#endif
}

int bound_port(NativeSocket socket) {
  sockaddr_in address{};
  SocketLength size = sizeof(address);
  if (::getsockname(socket, reinterpret_cast<sockaddr *>(&address), &size) != 0) {
    throw std::runtime_error("failed to inspect fake sensor socket");
  }
  return ntohs(address.sin_port);
}

void bind_loopback(NativeSocket socket, int type) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (::bind(socket, reinterpret_cast<const sockaddr *>(&address),
             static_cast<SocketLength>(sizeof(address))) != 0) {
    throw std::runtime_error(type == SOCK_DGRAM ? "failed to bind fake RDT socket"
                                                : "failed to bind fake HTTP socket");
  }
}

void put_u32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
  for (unsigned index = 0; index < 4; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> (24U - 8U * index));
  }
}

std::vector<std::uint8_t> make_record(std::uint32_t rdt_sequence, std::uint32_t status,
                                      std::uint32_t ft_sequence,
                                      const std::array<std::int32_t, 6> &axes) {
  std::vector<std::uint8_t> record(36);
  put_u32(record, 0, rdt_sequence);
  put_u32(record, 4, ft_sequence);
  put_u32(record, 8, status);
  for (std::size_t index = 0; index < axes.size(); ++index) {
    put_u32(record, 12 + 4 * index, static_cast<std::uint32_t>(axes[index]));
  }
  return record;
}

std::optional<std::uint16_t> decode_command(const std::uint8_t *bytes, std::size_t size) noexcept {
  if (size != 8 || bytes[0] != 0x12 || bytes[1] != 0x34) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[2]) << 8U) | bytes[3]);
}

void send_all(NativeSocket socket, std::string_view data) noexcept {
  while (!data.empty()) {
    const auto sent = send_bytes(socket, data.data(), data.size());
    if (sent <= 0) {
      return;
    }
    data.remove_prefix(static_cast<std::size_t>(sent));
  }
}

bool wait_until_count(const std::atomic<unsigned> &count, unsigned expected,
                      std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  do {
    if (count.load(std::memory_order_acquire) >= expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  } while (std::chrono::steady_clock::now() < deadline);
  return count.load(std::memory_order_acquire) >= expected;
}

} // namespace

class FakeSensor::Implementation {
public:
  explicit Implementation(double rate_hz)
      : interval_(std::chrono::duration<double>{1.0 / rate_hz}) {
    try {
      udp_socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
      if (!valid(udp_socket_)) {
        throw std::runtime_error("failed to create fake RDT socket");
      }
      bind_loopback(udp_socket_, SOCK_DGRAM);
      rdt_port_ = bound_port(udp_socket_);
      if (!set_nonblocking(udp_socket_)) {
        throw std::runtime_error("failed to configure fake RDT socket");
      }

      http_listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
      if (!valid(http_listener_)) {
        throw std::runtime_error("failed to create fake HTTP socket");
      }
      const int reuse = 1;
#ifdef _WIN32
      static_cast<void>(::setsockopt(http_listener_, SOL_SOCKET, SO_REUSEADDR,
                                     reinterpret_cast<const char *>(&reuse), sizeof(reuse)));
#else
      static_cast<void>(
          ::setsockopt(http_listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));
#endif
      bind_loopback(http_listener_, SOCK_STREAM);
      if (::listen(http_listener_, 8) != 0) {
        throw std::runtime_error("failed to listen on fake HTTP socket");
      }
      http_port_ = bound_port(http_listener_);

      udp_thread_ = std::thread([this] { run_udp(); });
      http_thread_ = std::thread([this] { run_http(); });
    } catch (...) {
      shutdown();
      throw;
    }
  }

  ~Implementation() { shutdown(); }

  void shutdown() noexcept {
    stopping_.store(true, std::memory_order_release);
    shutdown_socket(udp_socket_);
    shutdown_socket(http_listener_);
    {
      std::lock_guard<std::mutex> lock(http_client_mutex_);
      shutdown_socket(http_client_);
    }
    close_socket(http_listener_);
    if (udp_thread_.joinable()) {
      udp_thread_.join();
    }
    if (http_thread_.joinable()) {
      http_thread_.join();
    }
    close_socket(udp_socket_);
    std::lock_guard<std::mutex> lock(http_client_mutex_);
    close_socket(http_client_);
  }

  void run_udp() noexcept {
    auto next = std::chrono::steady_clock::now();
    while (!stopping_.load(std::memory_order_acquire)) {
      receive_command();
      const auto now = std::chrono::steady_clock::now();
      if (now >= next) {
        send_next();
        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval_);
        if (next < now) {
          next = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval_);
        }
      }
      std::this_thread::sleep_for(std::chrono::microseconds{100});
    }
  }

  void receive_command() noexcept {
    sockaddr_in peer{};
    SocketLength peer_size = sizeof(peer);
    std::array<std::uint8_t, 64> bytes{};
    const auto received = receive_from(udp_socket_, bytes.data(), bytes.size(),
                                       reinterpret_cast<sockaddr *>(&peer), &peer_size);
    if (received <= 0) {
      return;
    }
    const auto command = decode_command(bytes.data(), static_cast<std::size_t>(received));
    if (!command) {
      return;
    }
    if (*command == start_realtime) {
      {
        std::lock_guard<std::mutex> lock(udp_mutex_);
        client_ = peer;
        has_client_ = true;
        rdt_sequence_ = 0;
      }
      start_count_.fetch_add(1, std::memory_order_release);
      streaming_.store(true, std::memory_order_release);
    } else if (*command == stop_streaming) {
      stop_count_.fetch_add(1, std::memory_order_release);
      streaming_.store(false, std::memory_order_release);
    } else if (*command == set_software_bias) {
      bias_count_.fetch_add(1, std::memory_order_release);
      streaming_.store(false, std::memory_order_release);
    }
  }

  void send_next() noexcept {
    if (!enabled_.load(std::memory_order_acquire) || !streaming_.load(std::memory_order_acquire)) {
      return;
    }

    std::vector<std::uint8_t> record;
    sockaddr_in peer{};
    {
      std::lock_guard<std::mutex> lock(udp_mutex_);
      if (!has_client_) {
        return;
      }
      peer = client_;
      if (!records_.empty()) {
        record = std::move(records_.front());
        records_.pop_front();
      } else {
        rdt_sequence_ += 1 + skipped_records_;
        skipped_records_ = 0;
        record = make_record(rdt_sequence_, 0, ft_sequence_, {100, -200, 300, 10, -20, 30});
        ft_sequence_ += 4;
      }
    }
    static_cast<void>(send_to(udp_socket_, record.data(), record.size(),
                              reinterpret_cast<const sockaddr *>(&peer),
                              static_cast<SocketLength>(sizeof(peer))));
  }

  void run_http() noexcept {
    while (!stopping_.load(std::memory_order_acquire)) {
      NativeSocket client = ::accept(http_listener_, nullptr, nullptr);
      if (!valid(client)) {
        if (stopping_.load(std::memory_order_acquire)) {
          return;
        }
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(http_client_mutex_);
        http_client_ = client;
      }
      handle_http(client);
      {
        std::lock_guard<std::mutex> lock(http_client_mutex_);
        close_socket(http_client_);
      }
    }
  }

  void handle_http(NativeSocket client) noexcept {
    std::string request;
    std::array<char, 1024> bytes{};
    while (!stopping_.load(std::memory_order_acquire) && request.size() < 8192 &&
           request.find("\r\n\r\n") == std::string::npos) {
      const auto received = receive(client, bytes.data(), bytes.size());
      if (received <= 0) {
        return;
      }
      request.append(bytes.data(), static_cast<std::size_t>(received));
    }
    http_requests_.fetch_add(1, std::memory_order_release);

    std::string body;
    int status{};
    std::chrono::milliseconds delay{};
    {
      std::lock_guard<std::mutex> lock(http_response_mutex_);
      body = http_body_;
      status = http_status_;
      delay = http_delay_;
    }
    const auto deadline = std::chrono::steady_clock::now() + delay;
    while (!stopping_.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    if (stopping_.load(std::memory_order_acquire)) {
      return;
    }
    if (request.rfind("GET /netftapi2.xml ", 0) != 0) {
      status = 404;
      body.clear();
    }
    const std::string reason = status == 200 ? "OK" : "Error";
    const std::string headers =
        "HTTP/1.1 " + std::to_string(status) + " " + reason +
        "\r\nContent-Type: application/xml\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nConnection: close\r\n\r\n";
    send_all(client, headers);
    send_all(client, body);
  }

  SocketRuntime runtime_;
  const std::string host_{"127.0.0.1"};
  NativeSocket udp_socket_{invalid_socket};
  NativeSocket http_listener_{invalid_socket};
  NativeSocket http_client_{invalid_socket};
  int rdt_port_{};
  int http_port_{};
  std::chrono::duration<double> interval_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> enabled_{true};
  std::atomic<bool> streaming_{false};
  std::thread udp_thread_;
  std::thread http_thread_;

  std::mutex udp_mutex_;
  sockaddr_in client_{};
  bool has_client_{};
  std::deque<std::vector<std::uint8_t>> records_;
  std::uint32_t rdt_sequence_{};
  std::uint32_t ft_sequence_{1000};
  unsigned skipped_records_{};

  std::atomic<unsigned> start_count_{0};
  std::atomic<unsigned> stop_count_{0};
  std::atomic<unsigned> bias_count_{0};
  std::atomic<std::uint64_t> http_requests_{0};

  std::mutex http_client_mutex_;
  std::mutex http_response_mutex_;
  std::string http_body_{default_xml};
  int http_status_{200};
  std::chrono::milliseconds http_delay_{};
};

FakeSensor::FakeSensor(double rate_hz)
    : implementation_(std::make_unique<Implementation>(rate_hz)) {}

FakeSensor::~FakeSensor() = default;

const std::string &FakeSensor::host() const noexcept { return implementation_->host_; }

int FakeSensor::http_port() const noexcept { return implementation_->http_port_; }

int FakeSensor::rdt_port() const noexcept { return implementation_->rdt_port_; }

void FakeSensor::pause() noexcept {
  implementation_->enabled_.store(false, std::memory_order_release);
}

void FakeSensor::resume() noexcept {
  implementation_->enabled_.store(true, std::memory_order_release);
}

void FakeSensor::queue_record(std::uint32_t rdt_sequence, std::uint32_t status,
                              std::uint32_t ft_sequence, std::array<std::int32_t, 6> axes) {
  std::lock_guard<std::mutex> lock(implementation_->udp_mutex_);
  if (ft_sequence == 0) {
    ft_sequence = implementation_->ft_sequence_;
  }
  implementation_->ft_sequence_ = ft_sequence + 4;
  implementation_->records_.push_back(make_record(rdt_sequence, status, ft_sequence, axes));
}

void FakeSensor::skip_records(unsigned count) noexcept {
  std::lock_guard<std::mutex> lock(implementation_->udp_mutex_);
  implementation_->skipped_records_ += count;
}

void FakeSensor::set_http_response(std::string xml, int status) {
  std::lock_guard<std::mutex> lock(implementation_->http_response_mutex_);
  implementation_->http_body_ = std::move(xml);
  implementation_->http_status_ = status;
}

void FakeSensor::set_http_response_delay(std::chrono::milliseconds delay) {
  std::lock_guard<std::mutex> lock(implementation_->http_response_mutex_);
  implementation_->http_delay_ = delay;
}

bool FakeSensor::wait_for_start_realtime(unsigned count, std::chrono::milliseconds timeout) const {
  return wait_until_count(implementation_->start_count_, count, timeout);
}

bool FakeSensor::wait_for_stop_streaming(unsigned count, std::chrono::milliseconds timeout) const {
  return wait_until_count(implementation_->stop_count_, count, timeout);
}

unsigned FakeSensor::start_realtime_count() const noexcept {
  return implementation_->start_count_.load(std::memory_order_acquire);
}

unsigned FakeSensor::stop_streaming_count() const noexcept {
  return implementation_->stop_count_.load(std::memory_order_acquire);
}

unsigned FakeSensor::software_bias_count() const noexcept {
  return implementation_->bias_count_.load(std::memory_order_acquire);
}

std::uint64_t FakeSensor::http_request_count() const noexcept {
  return implementation_->http_requests_.load(std::memory_order_acquire);
}

} // namespace netft_cli::test
