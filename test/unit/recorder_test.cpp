#include "recording/recorder.hpp"

#include "app/error.hpp"
#include "support/assertions.hpp"
#include "support/fake_backend.hpp"
#include "support/fake_clock.hpp"
#include "support/options.hpp"
#include "support/records.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <stdexcept>
#include <vector>

namespace netft_cli {
namespace {

using namespace std::chrono_literals;

class FixedWallClock final : public WallClock {
public:
  [[nodiscard]] TimePoint now() const override { return TimePoint{1'000s}; }
};

class CollectingWriter final : public RecordingWriter {
public:
  void write(const RecordingRecord &record) override { records.push_back(record); }
  std::vector<RecordingRecord> records;
};

class FailingWriter final : public RecordingWriter {
public:
  void write(const RecordingRecord &) override { throw std::runtime_error("injected failure"); }
};

class BlockingWriter final : public RecordingWriter {
public:
  BlockingWriter(std::promise<void> &entered, std::shared_future<void> release)
      : entered_(entered), release_(std::move(release)) {}

  void write(const RecordingRecord &) override {
    if (!entered_reported_) {
      entered_reported_ = true;
      entered_.set_value();
      release_.wait();
    }
    ++written;
  }

  std::uint64_t written{};

private:
  std::promise<void> &entered_;
  std::shared_future<void> release_;
  bool entered_reported_{};
};

TEST(Recorder, StopsAtSampleCountBeforeDurationAndFinalizesAfterDrain) {
  test::FakeBackend backend;
  backend.session().set_samples({test::sample(1), test::sample(2), test::sample(3)});
  CollectingWriter writer;
  test::FakeClock clock;
  FixedWallClock wall_clock;
  InterruptFlag interrupt;
  Recorder recorder(backend, test::connection_options(), writer, clock, wall_clock, interrupt);
  std::size_t finalize_calls{};

  const auto result = recorder.run({1s, 2, 8}, [&] { ++finalize_calls; });

  EXPECT_EQ(result.written_count, 2U);
  EXPECT_FALSE(result.interrupted);
  ASSERT_EQ(writer.records.size(), 2U);
  EXPECT_EQ(writer.records[0].rdt_sequence, 1U);
  EXPECT_EQ(writer.records[1].rdt_sequence, 2U);
  EXPECT_EQ(finalize_calls, 1U);
  EXPECT_TRUE(clock.deadlines().empty());
  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(Recorder, StopsAtDurationAndFinalizesAllAcceptedSamples) {
  test::FakeBackend backend;
  backend.session().set_samples({test::sample(4)});
  CollectingWriter writer;
  test::FakeClock clock;
  FixedWallClock wall_clock;
  InterruptFlag interrupt;
  Recorder recorder(backend, test::connection_options(), writer, clock, wall_clock, interrupt);
  std::size_t finalize_calls{};

  const auto result = recorder.run({25ms, std::nullopt, 8}, [&] { ++finalize_calls; });

  EXPECT_EQ(result.written_count, 1U);
  EXPECT_EQ(finalize_calls, 1U);
  ASSERT_EQ(clock.deadlines().size(), 3U);
  EXPECT_EQ(clock.deadlines().back().time_since_epoch(), 25ms);
}

TEST(Recorder, InterruptDrainsAndFinalizesReadableOutput) {
  test::FakeBackend backend;
  backend.session().set_samples({test::sample(5), test::sample(6)});
  CollectingWriter writer;
  test::FakeClock clock;
  FixedWallClock wall_clock;
  InterruptFlag interrupt;
  interrupt.request();
  Recorder recorder(backend, test::connection_options(), writer, clock, wall_clock, interrupt);
  std::size_t finalize_calls{};

  const auto result = recorder.run({std::nullopt, std::nullopt, 8}, [&] { ++finalize_calls; });

  EXPECT_TRUE(result.interrupted);
  EXPECT_EQ(result.written_count, 2U);
  EXPECT_EQ(writer.records.size(), 2U);
  EXPECT_EQ(finalize_calls, 1U);
}

TEST(Recorder, WriterFailureIsAnIntegrityErrorAndDoesNotFinalize) {
  test::FakeBackend backend;
  backend.session().set_samples({test::sample(7)});
  FailingWriter writer;
  test::FakeClock clock;
  FixedWallClock wall_clock;
  InterruptFlag interrupt;
  Recorder recorder(backend, test::connection_options(), writer, clock, wall_clock, interrupt);
  bool finalized{};

  test::expect_app_error(ExitCode::Recording, [&] {
    static_cast<void>(recorder.run({20ms, std::nullopt, 8}, [&] { finalized = true; }));
  });

  EXPECT_FALSE(finalized);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(Recorder, QueueOverflowIsExplicitAndRetainsUnfinalizedData) {
  test::FakeBackend backend;
  backend.session().set_samples({test::sample(8), test::sample(9), test::sample(10)});
  std::promise<void> writer_entered;
  auto entered = writer_entered.get_future();
  std::promise<void> release_writer;
  auto release = release_writer.get_future().share();
  BlockingWriter writer(writer_entered, release);
  backend.session().set_after_sample([&](std::size_t count) {
    if (count == 1) {
      entered.wait();
    } else if (count == 3) {
      release_writer.set_value();
    }
  });
  test::FakeClock clock;
  FixedWallClock wall_clock;
  InterruptFlag interrupt;
  Recorder recorder(backend, test::connection_options(), writer, clock, wall_clock, interrupt);
  bool finalized{};

  test::expect_app_error(ExitCode::Recording, [&] {
    static_cast<void>(recorder.run({1s, std::nullopt, 1}, [&] { finalized = true; }));
  });

  EXPECT_FALSE(finalized);
  EXPECT_EQ(writer.written, 2U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(Recorder, NoFirstSampleFailsCountModeWithinConnectionTimeout) {
  test::FakeBackend backend;
  CollectingWriter writer;
  test::FakeClock clock;
  FixedWallClock wall_clock;
  InterruptFlag interrupt;
  Recorder recorder(backend, test::connection_options(), writer, clock, wall_clock, interrupt);
  bool finalized{};
  // An independent interrupt keeps the regression bounded on the old code.
  clock.set_sleep_hook([&](std::size_t n) {
    if (n == 200)
      interrupt.request();
  });
  test::expect_app_error(ExitCode::Recording, [&] {
    static_cast<void>(recorder.run({std::nullopt, 1, 8}, [&] { finalized = true; }));
  });
  EXPECT_FALSE(finalized);
  EXPECT_LE(clock.now().time_since_epoch(), 2s);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(Recorder, TerminalFaultRetainsPartialEvenAfterReceivingSamples) {
  test::FakeBackend backend;
  backend.session().set_samples({test::sample(10)});
  netft::HealthSnapshot health;
  health.state = netft::ClientState::Faulted;
  health.fault_code = netft::FaultCode::SeriousStatus;
  backend.session().set_health(health);
  CollectingWriter writer;
  test::FakeClock clock;
  FixedWallClock wall_clock;
  InterruptFlag interrupt;
  Recorder recorder(backend, test::connection_options(), writer, clock, wall_clock, interrupt);
  bool finalized{};
  test::expect_app_error(ExitCode::Recording, [&] {
    static_cast<void>(recorder.run({1s, std::nullopt, 8}, [&] { finalized = true; }));
  });
  EXPECT_FALSE(finalized);
  EXPECT_EQ(writer.records.size(), 1U);
}

} // namespace
} // namespace netft_cli
