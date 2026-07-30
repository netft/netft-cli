#include "stream/bounded_sample_queue.hpp"

#include "support/records.hpp"

#include <gtest/gtest.h>

#include <future>
#include <thread>
#include <vector>

namespace netft_cli {
namespace {

TEST(BoundedSampleQueue, DeliversCopiedSamplesInFifoOrder) {
  BoundedSampleQueue queue(3);
  auto first = test::sample(1);
  auto second = test::sample(2);

  EXPECT_EQ(queue.push(first), QueuePushResult::Accepted);
  EXPECT_EQ(queue.push(second), QueuePushResult::Accepted);
  first.rdt_sequence = 99;

  const auto first_result = queue.wait_pop();
  const auto second_result = queue.wait_pop();
  ASSERT_TRUE(first_result.sample);
  ASSERT_TRUE(second_result.sample);
  EXPECT_EQ(first_result.sample->rdt_sequence, 1U);
  EXPECT_EQ(second_result.sample->rdt_sequence, 2U);
}

TEST(BoundedSampleQueue, ReportsOverflowWithoutReplacingQueuedData) {
  BoundedSampleQueue queue(1);
  EXPECT_EQ(queue.push(test::sample(1)), QueuePushResult::Accepted);
  EXPECT_EQ(queue.push(test::sample(2)), QueuePushResult::Overflow);

  const auto result = queue.wait_pop();
  ASSERT_TRUE(result.sample);
  EXPECT_EQ(result.sample->rdt_sequence, 1U);
}

TEST(BoundedSampleQueue, CloseRejectsProducersAndLetsConsumerDrain) {
  BoundedSampleQueue queue(2);
  EXPECT_EQ(queue.push(test::sample(3)), QueuePushResult::Accepted);
  queue.close();

  EXPECT_EQ(queue.push(test::sample(4)), QueuePushResult::Closed);
  EXPECT_EQ(queue.wait_pop().status, QueuePopStatus::Item);
  EXPECT_EQ(queue.wait_pop().status, QueuePopStatus::Closed);
}

TEST(BoundedSampleQueue, PublishingWakesAWaitingConsumer) {
  BoundedSampleQueue queue(1);
  std::promise<void> entered;
  auto result = std::async(std::launch::async, [&] {
    entered.set_value();
    return queue.wait_pop();
  });
  entered.get_future().wait();

  EXPECT_EQ(queue.push(test::sample(5)), QueuePushResult::Accepted);
  const auto popped = result.get();
  ASSERT_TRUE(popped.sample);
  EXPECT_EQ(popped.sample->rdt_sequence, 5U);
}

TEST(BoundedSampleQueue, CancellationWakesConsumerAndDiscardsPendingData) {
  BoundedSampleQueue queue(2);
  EXPECT_EQ(queue.push(test::sample(6)), QueuePushResult::Accepted);
  queue.cancel();

  EXPECT_EQ(queue.push(test::sample(7)), QueuePushResult::Cancelled);
  const auto result = queue.wait_pop();
  EXPECT_EQ(result.status, QueuePopStatus::Cancelled);
  EXPECT_FALSE(result.sample);
}

TEST(BoundedSampleQueue, ConcurrentHandoffDoesNotLoseOrDuplicateSamples) {
  constexpr std::uint32_t count = 100;
  BoundedSampleQueue queue(count);
  std::vector<std::uint32_t> received;
  received.reserve(count);
  std::thread consumer([&] {
    for (;;) {
      const auto result = queue.wait_pop();
      if (result.status == QueuePopStatus::Closed) {
        return;
      }
      ASSERT_EQ(result.status, QueuePopStatus::Item);
      received.push_back(result.sample->rdt_sequence);
    }
  });

  for (std::uint32_t sequence = 1; sequence <= count; ++sequence) {
    ASSERT_EQ(queue.push(test::sample(sequence)), QueuePushResult::Accepted);
  }
  queue.close();
  consumer.join();

  ASSERT_EQ(received.size(), count);
  for (std::uint32_t index = 0; index < count; ++index) {
    EXPECT_EQ(received[index], index + 1);
  }
}

} // namespace
} // namespace netft_cli
