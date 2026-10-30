#include "vipee/wire.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace vipee {
namespace {

WireRecord SpeedUpdate() {
  WireRecord r;
  r.version = kWireVersion;
  r.kind = static_cast<uint16_t>(WireKind::kUpdate);
  r.prop_id = 0x11600207;  // PERF_VEHICLE_SPEED
  r.status = 2;            // ERROR
  r.int_value = -7;
  r.float_value = -5.0f;
  r.reserved = 0;
  r.timestamp_ns = 123456789012345LL;
  return r;
}

TEST(Wire, RecordIs32BytesWithVersion1) {
  EXPECT_EQ(sizeof(WireRecord), 32u);
  EXPECT_EQ(kWireVersion, 1);
  EXPECT_EQ(static_cast<uint16_t>(WireKind::kUpdate), 1);
  EXPECT_EQ(static_cast<uint16_t>(WireKind::kHeartbeat), 2);
}

TEST(Wire, RoundTripPreservesAllFields) {
  const WireRecord in = SpeedUpdate();
  std::array<uint8_t, kWireRecordSize> buf{};
  encode(in, buf.data());
  WireRecord out{};
  ASSERT_EQ(decode(buf.data(), buf.size(), &out), WireError::kNone);
  EXPECT_EQ(out.version, in.version);
  EXPECT_EQ(out.kind, in.kind);
  EXPECT_EQ(out.prop_id, in.prop_id);
  EXPECT_EQ(out.status, in.status);
  EXPECT_EQ(out.int_value, in.int_value);
  EXPECT_EQ(out.float_value, in.float_value);
  EXPECT_EQ(out.reserved, in.reserved);
  EXPECT_EQ(out.timestamp_ns, in.timestamp_ns);
}

TEST(Wire, LayoutIsLittleEndianAtDesignOffsets) {
  std::array<uint8_t, kWireRecordSize> buf{};
  encode(SpeedUpdate(), buf.data());
  EXPECT_EQ(buf[0], 1);  // version low byte
  EXPECT_EQ(buf[1], 0);
  EXPECT_EQ(buf[2], 1);  // kind = update
  EXPECT_EQ(buf[4], 0x07);
  EXPECT_EQ(buf[7], 0x11);  // prop_id high byte
  EXPECT_EQ(buf[8], 2);     // status
  EXPECT_EQ(buf[24], static_cast<uint8_t>(123456789012345LL & 0xff));
}

TEST(Wire, UnknownVersionIsRejected) {
  WireRecord in = SpeedUpdate();
  in.version = 2;
  std::array<uint8_t, kWireRecordSize> buf{};
  encode(in, buf.data());
  WireRecord out{};
  EXPECT_EQ(decode(buf.data(), buf.size(), &out), WireError::kUnknownVersion);
}

TEST(Wire, WrongSizeIsRejected) {
  std::array<uint8_t, kWireRecordSize> buf{};
  encode(SpeedUpdate(), buf.data());
  WireRecord out{};
  EXPECT_EQ(decode(buf.data(), kWireRecordSize - 1, &out), WireError::kBadSize);
}

}  // namespace
}  // namespace vipee
