#include "vipee/latest_slot.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <thread>

namespace vipee {
namespace {

using std::chrono::milliseconds;
using Clock = std::chrono::steady_clock;

TEST(LatestSlot, TwoPutsBeforeTakeDeliverOnlyTheNewest) {
  LatestSlot<int> slot;
  slot.put(1);
  slot.put(2);
  EXPECT_EQ(slot.take(milliseconds(0)), 2);
  EXPECT_EQ(slot.take(milliseconds(0)), std::nullopt);
}

TEST(LatestSlot, EmptyTakeReturnsNulloptAfterTheTimeout) {
  LatestSlot<int> slot;
  const auto start = Clock::now();
  EXPECT_EQ(slot.take(milliseconds(50)), std::nullopt);
  EXPECT_GE(Clock::now() - start, milliseconds(50));
}

TEST(LatestSlot, PutWakesABlockedTake) {
  LatestSlot<int> slot;
  auto taken = std::async(std::launch::async,
                          [&] { return slot.take(milliseconds(10000)); });
  std::this_thread::sleep_for(milliseconds(20));
  slot.put(7);
  ASSERT_EQ(taken.wait_for(milliseconds(2000)), std::future_status::ready);
  EXPECT_EQ(taken.get(), 7);
}

TEST(LatestSlot, CloseWakesABlockedTake) {
  LatestSlot<int> slot;
  auto taken = std::async(std::launch::async,
                          [&] { return slot.take(milliseconds(10000)); });
  std::this_thread::sleep_for(milliseconds(20));
  slot.close();
  ASSERT_EQ(taken.wait_for(milliseconds(2000)), std::future_status::ready);
  EXPECT_EQ(taken.get(), std::nullopt);
  EXPECT_EQ(slot.take(milliseconds(10000)), std::nullopt);
}

TEST(LatestSlot, PutsWithoutTakeKeepOneValue) {
  LatestSlot<std::shared_ptr<int>> slot;
  std::weak_ptr<int> first;
  std::weak_ptr<int> second;
  {
    auto a = std::make_shared<int>(1);
    auto b = std::make_shared<int>(2);
    first = a;
    second = b;
    slot.put(std::move(a));
    slot.put(std::move(b));
  }
  EXPECT_TRUE(first.expired());
  slot.put(std::make_shared<int>(3));
  EXPECT_TRUE(second.expired());
  auto got = slot.take(milliseconds(0));
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(**got, 3);
}

}  // namespace
}  // namespace vipee
