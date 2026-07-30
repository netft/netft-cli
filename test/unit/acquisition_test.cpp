#include "stream/acquisition.hpp"

#include "app/error.hpp"
#include "support/assertions.hpp"
#include "support/fake_backend.hpp"
#include "support/options.hpp"
#include "support/records.hpp"

#include <gtest/gtest.h>

#include <cstddef>

namespace netft_cli {
namespace {

TEST(Acquisition, OwnsSessionStartHealthAndStopLifecycle) {
  test::FakeBackend backend;
  backend.session().set_samples({test::sample(4)});
  backend.session().set_health(test::health());
  std::size_t callback_count{};

  {
    auto acquisition = Acquisition::open(backend, test::connection_options());
    acquisition.start([&](const netft::Sample &) { ++callback_count; });

    EXPECT_EQ(callback_count, 1U);
    EXPECT_EQ(acquisition.health().state, netft::ClientState::Streaming);
    acquisition.stop();
    acquisition.stop();
  }

  EXPECT_EQ(backend.open_calls(), 1U);
  EXPECT_EQ(backend.session().start_calls(), 1U);
  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(Acquisition, StopsAnActiveSessionWhenScopeEnds) {
  test::FakeBackend backend;
  backend.session().set_samples({test::sample(5)});

  {
    auto acquisition = Acquisition::open(backend, test::connection_options());
    acquisition.start([](const netft::Sample &) {});
  }

  EXPECT_EQ(backend.session().stop_calls(), 1U);
}

TEST(Acquisition, TranslatesOpenAndStartFailuresToStreamErrors) {
  test::FakeBackend backend;
  backend.fail_open();
  test::expect_app_error(ExitCode::Stream, [&] {
    static_cast<void>(Acquisition::open(backend, test::connection_options()));
  });

  test::FakeBackend start_backend;
  start_backend.session().fail_start();
  auto acquisition = Acquisition::open(start_backend, test::connection_options());
  test::expect_app_error(ExitCode::Stream,
                         [&] { acquisition.start([](const netft::Sample &) {}); });
}

} // namespace
} // namespace netft_cli
