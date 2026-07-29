#include "platform/line_reader.hpp"

#include "platform/interrupt.hpp"

#include <gtest/gtest.h>

#ifndef _WIN32

#include <unistd.h>

#include <chrono>
#include <future>
#include <string_view>

namespace netft_cli {
namespace {

using namespace std::chrono_literals;

TEST(NativeLineReader, ReadsLineFromReadyDescriptor) {
  int descriptors[2]{};
  ASSERT_EQ(::pipe(descriptors), 0);
  constexpr std::string_view input{"yes\n"};
  ASSERT_EQ(::write(descriptors[1], input.data(), input.size()),
            static_cast<ssize_t>(input.size()));
  ASSERT_EQ(::close(descriptors[1]), 0);
  auto reader = make_interruptible_line_reader(descriptors[0]);
  InterruptFlag interrupt;

  const auto result = reader->read_line(interrupt);

  EXPECT_EQ(result.status, LineReadStatus::Line);
  EXPECT_EQ(result.line, "yes");
  EXPECT_EQ(::close(descriptors[0]), 0);
}

TEST(NativeLineReader, ReturnsEofWhenDescriptorClosesWithoutInput) {
  int descriptors[2]{};
  ASSERT_EQ(::pipe(descriptors), 0);
  ASSERT_EQ(::close(descriptors[1]), 0);
  auto reader = make_interruptible_line_reader(descriptors[0]);
  InterruptFlag interrupt;

  const auto result = reader->read_line(interrupt);

  EXPECT_EQ(result.status, LineReadStatus::Eof);
  EXPECT_TRUE(result.line.empty());
  EXPECT_EQ(::close(descriptors[0]), 0);
}

TEST(NativeLineReader, ObservesInterruptWithoutAdditionalInput) {
  int descriptors[2]{};
  ASSERT_EQ(::pipe(descriptors), 0);
  auto reader = make_interruptible_line_reader(descriptors[0]);
  InterruptFlag interrupt;
  auto result = std::async(std::launch::async, [&] { return reader->read_line(interrupt); });
  ASSERT_EQ(result.wait_for(20ms), std::future_status::timeout);

  interrupt.request();
  const auto readiness = result.wait_for(250ms);
  if (readiness != std::future_status::ready) {
    static_cast<void>(::close(descriptors[1]));
  }
  ASSERT_EQ(readiness, std::future_status::ready);
  EXPECT_EQ(result.get().status, LineReadStatus::Interrupted);

  EXPECT_EQ(::close(descriptors[0]), 0);
  EXPECT_EQ(::close(descriptors[1]), 0);
}

} // namespace
} // namespace netft_cli

#endif
