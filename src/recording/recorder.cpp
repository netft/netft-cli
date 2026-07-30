#include "recording/recorder.hpp"

#include "app/error.hpp"
#include "stream/acquisition.hpp"
#include "stream/bounded_sample_queue.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <thread>
#include <utility>

namespace netft_cli {

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

  std::optional<Clock::TimePoint> deadline;
  if (limits.duration) {
    const auto ticks = std::chrono::duration_cast<Clock::Duration>(*limits.duration);
    if (ticks <= Clock::Duration::zero() || monotonic_origin > Clock::TimePoint::max() - ticks) {
      acquisition.stop();
      queue.cancel();
      writer_thread.join();
      throw AppError{ExitCode::Usage, "duration is outside the supported clock range"};
    }
    deadline = monotonic_origin + ticks;
  }

  bool interrupted{};
  constexpr auto poll_interval = std::chrono::milliseconds{10};
  while (!stop_requested.load(std::memory_order_acquire)) {
    if (interrupt_.requested()) {
      interrupted = true;
      break;
    }
    const auto now = clock_.now();
    if (deadline && now >= *deadline) {
      break;
    }
    const auto interval = std::chrono::duration_cast<Clock::Duration>(poll_interval);
    const auto next_poll =
        now > Clock::TimePoint::max() - interval ? Clock::TimePoint::max() : now + interval;
    const auto next = deadline ? std::min(*deadline, next_poll) : next_poll;
    if (!clock_.wait_until(next, interrupt_)) {
      interrupted = true;
      break;
    }
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
  finalize();
  return {written_count, interrupted};
}

} // namespace netft_cli
