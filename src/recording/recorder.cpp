#include "recording/recorder.hpp"

#include "app/error.hpp"
#include "stream/acquisition.hpp"
#include "stream/bounded_sample_queue.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <thread>
#include <utility>

namespace netft_cli {
namespace {
Clock::TimePoint deadline_after(Clock::TimePoint origin, std::chrono::duration<double> duration) {
  const auto ticks = static_cast<long double>(duration.count()) * Clock::Duration::period::den /
                     Clock::Duration::period::num;
  if (!std::isfinite(ticks) || ticks < 1 ||
      ticks > static_cast<long double>(Clock::Duration::max().count())) {
    throw AppError{ExitCode::Usage, "duration is outside the supported clock range"};
  }
  const auto converted = std::chrono::duration_cast<Clock::Duration>(duration);
  if (origin > Clock::TimePoint::max() - converted) {
    throw AppError{ExitCode::Usage, "duration is outside the supported clock range"};
  }
  return origin + converted;
}
} // namespace

Recorder::Recorder(SensorBackend &backend, ConnectionOptions connection, RecordingWriter &writer,
                   Clock &clock, WallClock &wall_clock, InterruptFlag &interrupt)
    : backend_(backend), connection_(std::move(connection)), writer_(writer), clock_(clock),
      wall_clock_(wall_clock), interrupt_(interrupt) {}

RecorderResult Recorder::run(const RecorderLimits &limits, const std::function<void()> &finalize) {
  BoundedSampleQueue queue(limits.queue_capacity);
  std::atomic<bool> stop_requested{};
  std::atomic<bool> overflow{};
  std::atomic<bool> writer_failed{};
  std::atomic<std::uint64_t> accepted_count{};
  std::uint64_t written_count{};
  std::exception_ptr writer_error;
  const auto monotonic_origin = clock_.now();
  const auto wall_origin = wall_clock_.now();
  const auto initial_data_deadline = deadline_after(monotonic_origin, connection_.timeout);
  const auto idle_interval = initial_data_deadline - monotonic_origin;
  std::optional<Clock::TimePoint> deadline;
  if (limits.duration)
    deadline = deadline_after(monotonic_origin, *limits.duration);

  auto acquisition = Acquisition::open(backend_, connection_);

  std::thread writer_thread([&] {
    try {
      for (;;) {
        const auto item = queue.wait_pop();
        if (item.status != QueuePopStatus::Item) {
          return;
        }
        const auto timestamp =
            wall_origin + std::chrono::duration_cast<WallClock::TimePoint::duration>(
                              item.sample->received_at - monotonic_origin);
        writer_.write(make_recording_record(*item.sample, monotonic_origin, timestamp));
        ++written_count;
      }
    } catch (...) {
      writer_error = std::current_exception();
      writer_failed.store(true, std::memory_order_release);
      stop_requested.store(true, std::memory_order_release);
      queue.cancel();
    }
  });

  try {
    acquisition.start([&](const netft::Sample &sample) {
      if (stop_requested.load(std::memory_order_acquire)) {
        return;
      }
      if (limits.count && accepted_count.load(std::memory_order_relaxed) >= *limits.count) {
        stop_requested.store(true, std::memory_order_release);
        return;
      }
      const auto pushed = queue.push(sample);
      if (pushed == QueuePushResult::Overflow) {
        overflow.store(true, std::memory_order_release);
        stop_requested.store(true, std::memory_order_release);
        return;
      }
      if (pushed != QueuePushResult::Accepted) {
        return;
      }
      const auto count = accepted_count.fetch_add(1, std::memory_order_acq_rel) + 1;
      if (limits.count && count >= *limits.count) {
        stop_requested.store(true, std::memory_order_release);
      }
    });
  } catch (...) {
    queue.cancel();
    writer_thread.join();
    throw;
  }

  bool interrupted{};
  bool data_failed{};
  std::uint64_t last_progress_count{};
  auto data_deadline = initial_data_deadline;
  constexpr auto poll_interval = std::chrono::milliseconds{10};
  try {
    while (!stop_requested.load(std::memory_order_acquire)) {
      if (interrupt_.requested()) {
        interrupted = true;
        break;
      }
      const auto now = clock_.now();
      const auto health = acquisition.health();
      if (health.state == netft::ClientState::Faulted) {
        data_failed = true;
        break;
      }
      const auto count = accepted_count.load(std::memory_order_acquire);
      if (count != last_progress_count) {
        last_progress_count = count;
        data_deadline = now > Clock::TimePoint::max() - idle_interval ? Clock::TimePoint::max()
                                                                      : now + idle_interval;
      }
      if (now >= data_deadline) {
        data_failed = true;
        break;
      }
      if (deadline && now >= *deadline) {
        break;
      }
      const auto interval = std::chrono::duration_cast<Clock::Duration>(poll_interval);
      const auto next_poll =
          now > Clock::TimePoint::max() - interval ? Clock::TimePoint::max() : now + interval;
      const auto next =
          std::min(data_deadline, deadline ? std::min(*deadline, next_poll) : next_poll);
      if (!clock_.wait_until(next, interrupt_)) {
        interrupted = true;
        break;
      }
    }

    const auto final_health = acquisition.health();
    data_failed = data_failed || final_health.state == netft::ClientState::Faulted;
  } catch (...) {
    data_failed = true;
  }
  acquisition.stop();
  queue.close();
  writer_thread.join();

  if (writer_failed.load(std::memory_order_acquire)) {
    static_cast<void>(writer_error);
    throw AppError{ExitCode::Recording, "recording writer failed; partial output was retained"};
  }
  if (overflow.load(std::memory_order_acquire)) {
    throw AppError{ExitCode::Recording, "recording queue overflowed; partial output was retained"};
  }
  if (data_failed || written_count == 0) {
    throw AppError{ExitCode::Recording, "recording received no data within timeout or reached a "
                                        "terminal fault; partial output was retained"};
  }
  finalize();
  return {written_count, interrupted};
}

} // namespace netft_cli
