#include "vipee/vhal_ingest.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace vipee {
namespace {

constexpr int64_t kMs = 1000000;
constexpr int32_t kSpeed = 0x11600207;
constexpr int32_t kGearSelection = 0x11400400;
constexpr int32_t kParkingBrake = 0x11200402;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
WireRecord update(int32_t prop, int32_t status, int64_t ts) {
  WireRecord r{};
  r.version = kWireVersion;
  r.kind = static_cast<uint16_t>(WireKind::kUpdate);
  r.prop_id = prop;
  r.status = status;
  r.timestamp_ns = ts;
  return r;
}

WireRecord heartbeat() {
  WireRecord r{};
  r.version = kWireVersion;
  r.kind = static_cast<uint16_t>(WireKind::kHeartbeat);
  return r;
}

IngestResult feed(VhalIngest& in, const WireRecord& r, int64_t now) {
  uint8_t buf[kWireRecordSize];
  encode(r, buf);
  return in.onDatagram(buf, sizeof buf, now);
}

// CTS-11: a peer that goes quiet for 300 ms makes the VHAL drop to UNAVAILABLE.
TEST(VhalIngest, WatchdogFiresOnceAfter300MsWithoutRecords) {
  VhalIngest in;
  in.onConnect(0);
  EXPECT_EQ(feed(in, heartbeat(), 100 * kMs).action, IngestAction::kAlive);
  EXPECT_FALSE(in.expire(400 * kMs));
  EXPECT_TRUE(in.expire(401 * kMs));
  EXPECT_FALSE(in.expire(500 * kMs));
  EXPECT_FALSE(in.alive());
}

TEST(VhalIngest, NoWatchdogWithoutPeer) {
  VhalIngest in;
  EXPECT_FALSE(in.expire(10000 * kMs));
}

TEST(VhalIngest, DisconnectOfLivePeerMarksOnce) {
  VhalIngest in;
  in.onConnect(0);
  EXPECT_TRUE(in.onDisconnect(50 * kMs));
  EXPECT_FALSE(in.onDisconnect(60 * kMs));
  EXPECT_EQ(in.floorNs(), 50 * kMs);
}

// A resync after the watchdog carries the original arrival stamps; they are
// raised to the UNAVAILABLE marker so the store and CarService keep them.
TEST(VhalIngest, RecordsOlderThanMarkerAreRaisedToIt) {
  VhalIngest in;
  in.onConnect(0);
  ASSERT_TRUE(in.expire(301 * kMs));
  in.onConnect(320 * kMs);
  IngestResult r = feed(in, update(kGearSelection, 0, 200 * kMs), 330 * kMs);
  ASSERT_EQ(r.action, IngestAction::kWrite);
  EXPECT_EQ(r.record.timestamp_ns, 301 * kMs);
  r = feed(in, update(kGearSelection, 0, 340 * kMs), 345 * kMs);
  EXPECT_EQ(r.record.timestamp_ns, 340 * kMs);
}

// CTS-11: only valid records keep the peer alive; others are dropped, named.
TEST(VhalIngest, UnknownVersionIsDroppedAndDoesNotFeedWatchdog) {
  VhalIngest in;
  in.onConnect(0);
  WireRecord r = heartbeat();
  r.version = kWireVersion + 1;
  IngestResult res = feed(in, r, 200 * kMs);
  EXPECT_EQ(res.action, IngestAction::kDrop);
  EXPECT_EQ(res.drop, DropReason::kUnknownVersion);
  EXPECT_TRUE(in.expire(301 * kMs));
}

TEST(VhalIngest, BadSizeUnknownPropAndBadStatusAreDropped) {
  VhalIngest in;
  in.onConnect(0);
  uint8_t buf[kWireRecordSize] = {};
  EXPECT_EQ(in.onDatagram(buf, kWireRecordSize - 1, 1).drop,
            DropReason::kBadSize);
  EXPECT_EQ(feed(in, update(0x11400405, 0, 1), 1).drop,
            DropReason::kUnknownProp);
  EXPECT_EQ(feed(in, update(kParkingBrake, 3, 1), 1).drop,
            DropReason::kBadStatus);
  WireRecord odd = heartbeat();
  odd.kind = 9;
  EXPECT_EQ(feed(in, odd, 1).drop, DropReason::kUnknownKind);
}

// CTS-10 / design: speed arrives at ~66 Hz and is decimated to the
// subscribed 10 Hz.
TEST(VhalIngest, SpeedIsDecimatedToSubscribedRate) {
  VhalIngest in;
  in.onConnect(0);
  in.setSpeedRateHz(10.0f);
  int emitted = 0;
  for (int64_t t = 0; t < 1000 * kMs; t += 15 * kMs) {
    if (feed(in, update(kSpeed, 0, t), t).emit) ++emitted;
  }
  EXPECT_GE(emitted, 9);
  EXPECT_LE(emitted, 11);
}

TEST(VhalIngest, SpeedStatusChangeIsAlwaysEmitted) {
  VhalIngest in;
  in.onConnect(0);
  in.setSpeedRateHz(1.0f);
  EXPECT_TRUE(feed(in, update(kSpeed, 0, 0), 0).emit);
  EXPECT_FALSE(feed(in, update(kSpeed, 0, 15 * kMs), 15 * kMs).emit);
  EXPECT_TRUE(feed(in, update(kSpeed, 1, 30 * kMs), 30 * kMs).emit);
}

TEST(VhalIngest, OnChangePropertiesAreNotDecimated) {
  VhalIngest in;
  in.onConnect(0);
  in.setSpeedRateHz(1.0f);
  EXPECT_TRUE(feed(in, update(kGearSelection, 0, 0), 0).emit);
  EXPECT_TRUE(feed(in, update(kGearSelection, 0, 1 * kMs), 1 * kMs).emit);
}

TEST(VhalIngest, UnsubscribedSpeedIsNotDecimated) {
  VhalIngest in;
  in.onConnect(0);
  EXPECT_TRUE(feed(in, update(kSpeed, 0, 0), 0).emit);
  EXPECT_TRUE(feed(in, update(kSpeed, 0, 1 * kMs), 1 * kMs).emit);
}

}  // namespace
}  // namespace vipee
