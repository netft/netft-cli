#include "platform/interrupt.hpp"

#include <gtest/gtest.h>

#include <thread>

#ifndef _WIN32
#include <csignal>
#endif

namespace netft_cli {
namespace {

TEST(InterruptFlag, StartsWithoutARequest) {
  const InterruptFlag flag;
  EXPECT_FALSE(flag.requested());
}

TEST(InterruptFlag, RequestIsVisibleAcrossThreads) {
  InterruptFlag flag;
  std::thread worker([&] { flag.request(); });
  worker.join();
  EXPECT_TRUE(flag.requested());
}

#ifndef _WIN32

volatile std::sig_atomic_t previous_handler_called = 0;

extern "C" void record_previous_handler(int) { previous_handler_called = 1; }

TEST(InterruptHandler, RequestsFlagAndRestoresPreviousHandler) {
  struct sigaction original_action{};
  ASSERT_EQ(::sigaction(SIGINT, nullptr, &original_action), 0);

  struct sigaction previous_action{};
  previous_action.sa_handler = record_previous_handler;
  ASSERT_EQ(sigemptyset(&previous_action.sa_mask), 0);
  previous_action.sa_flags = 0;
  ASSERT_EQ(::sigaction(SIGINT, &previous_action, nullptr), 0);

  InterruptFlag flag;
  {
    InterruptHandler handler(flag);
    ASSERT_EQ(::raise(SIGINT), 0);
    EXPECT_TRUE(flag.requested());
    EXPECT_EQ(previous_handler_called, 0);
  }

  ASSERT_EQ(::raise(SIGINT), 0);
  EXPECT_EQ(previous_handler_called, 1);
  ASSERT_EQ(::sigaction(SIGINT, &original_action, nullptr), 0);
}

#endif

} // namespace
} // namespace netft_cli
