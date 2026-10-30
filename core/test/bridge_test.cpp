#include "vipee/bridge.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "test_maps.h"

namespace vipee {
namespace {

constexpr int64_t kMs = 1000000;
constexpr int64_t kT0 = 1000 * kMs;

struct LogEntry {
  int32_t prop;
  Status status;
  Reason reason;
};

class FakeSink : public PropertySink {
 public:
  void publish(const PropertyUpdate& u) override { updates.push_back(u); }
  std::vector<PropertyUpdate> of(int32_t prop) const {
    std::vector<PropertyUpdate> r;
    for (const auto& u : updates) {
      if (u.prop_id == prop) r.push_back(u);
    }
    return r;
  }
  std::vector<PropertyUpdate> updates;
};

class FakeLogger : public Logger {
 public:
  void transition(int32_t prop, Status status, Reason reason) override {
    entries.push_back({prop, status, reason});
  }
  std::vector<LogEntry> of(int32_t prop) const {
    std::vector<LogEntry> r;
    for (const auto& e : entries) {
      if (e.prop == prop) r.push_back(e);
    }
    return r;
  }
  std::vector<LogEntry> entries;
};

constexpr int32_t kAllFour[] = {prop::kPerfVehicleSpeed, prop::kGearSelection,
                                prop::kCurrentGear, prop::kParkingBrakeOn};

class BridgeTest : public ::testing::Test {
 protected:
  // Starts the bridge and drops the initial-state records.
  Bridge& Start(const VehicleMap& map) {
    bridge_.emplace(map, sink_, log_, kT0);
    sink_.updates.clear();
    log_.entries.clear();
    return *bridge_;
  }
  PropertyUpdate Last(int32_t prop) const { return sink_.of(prop).back(); }

  FakeSink sink_;
  FakeLogger log_;
  std::optional<Bridge> bridge_;
};

// CTS-11
TEST_F(BridgeTest, StartsWithAllFourUnavailableNoData) {
  bridge_.emplace(Cat793fMap(), sink_, log_, kT0);
  for (int32_t p : kAllFour) {
    ASSERT_EQ(sink_.of(p).size(), 1u) << p;
    EXPECT_EQ(Last(p).status, Status::kUnavailable) << p;
    ASSERT_EQ(log_.of(p).size(), 1u) << p;
    EXPECT_EQ(log_.of(p)[0].status, Status::kUnavailable) << p;
    EXPECT_EQ(log_.of(p)[0].reason, Reason::kNoData) << p;
  }
  sink_.updates.clear();
  log_.entries.clear();
  bridge_->tick(kT0 + 10000 * kMs);
  EXPECT_TRUE(sink_.updates.empty());
  EXPECT_TRUE(log_.entries.empty());
}

// CTS-01, CTS-09
TEST_F(BridgeTest, Speed36KmhIs10MpsAvailableStampedAtArrival) {
  Start(Cat793fMap()).onTelemetry({36.0, 1}, kT0 + 7);
  const PropertyUpdate u = Last(prop::kPerfVehicleSpeed);
  EXPECT_EQ(u.status, Status::kAvailable);
  EXPECT_FLOAT_EQ(u.float_value, 10.0f);
  EXPECT_EQ(u.timestamp_ns, kT0 + 7);
}

// CTS-02
TEST_F(BridgeTest, NegativeSpeedKeptWhateverTheGear) {
  Bridge& b = Start(CommanderMap());
  b.onTelemetry({-18.0, 2}, kT0);
  EXPECT_FLOAT_EQ(Last(prop::kPerfVehicleSpeed).float_value, -5.0f);
  b.onTelemetry({-18.0, -1}, kT0 + kMs);
  EXPECT_FLOAT_EQ(Last(prop::kPerfVehicleSpeed).float_value, -5.0f);
  EXPECT_EQ(Last(prop::kPerfVehicleSpeed).status, Status::kAvailable);
}

// CTS-02 edge case
TEST_F(BridgeTest, NegativeZeroSpeedIsPositiveZero) {
  Start(CommanderMap()).onTelemetry({-0.0, 0}, kT0);
  EXPECT_EQ(Last(prop::kPerfVehicleSpeed).float_value, 0.0f);
  EXPECT_FALSE(std::signbit(Last(prop::kPerfVehicleSpeed).float_value));
}

// CTS-03
TEST_F(BridgeTest, CurrentGearFromGenericTable) {
  Bridge& b = Start(Cat793fMap());
  b.onTelemetry({0.0, 3}, kT0);
  EXPECT_EQ(Last(prop::kCurrentGear).int_value, 0x0040);
  EXPECT_EQ(Last(prop::kCurrentGear).status, Status::kAvailable);
  b.onTelemetry({0.0, -2}, kT0 + kMs);
  EXPECT_EQ(Last(prop::kCurrentGear).int_value, 0x0002);
  b.onTelemetry({0.0, 0}, kT0 + 2 * kMs);
  EXPECT_EQ(Last(prop::kCurrentGear).int_value, 0x0001);
}

// CTS-05
TEST_F(BridgeTest, DerivedSelectionIsReverseNeutralDrive) {
  Bridge& b = Start(CommanderMap());
  b.onTelemetry({0.0, -1}, kT0);
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0002);
  EXPECT_EQ(Last(prop::kGearSelection).status, Status::kAvailable);
  b.onTelemetry({0.0, 0}, kT0 + kMs);
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0001);
  b.onTelemetry({0.0, 4}, kT0 + 2 * kMs);
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0008);
}

// CTS-06
TEST_F(BridgeTest, DerivedSelectionNeverPublishesPark) {
  Bridge& b = Start(CommanderMap());
  int64_t t = kT0;
  for (int g = -12; g <= 12; ++g) b.onTelemetry({0.0, g}, t += kMs);
  for (const auto& u : sink_.of(prop::kGearSelection)) {
    EXPECT_NE(u.int_value, 0x0004);
  }
  EXPECT_FALSE(sink_.of(prop::kGearSelection).empty());
}

// CTS-08
TEST_F(BridgeTest, OnChangePropertiesDoNotRepeatEvents) {
  Bridge& b = Start(CommanderMap());
  b.onTelemetry({36.0, 2}, kT0);
  b.onTelemetry({36.0, 2}, kT0 + 15 * kMs);
  b.onTelemetry({36.0, 3}, kT0 + 30 * kMs);  // still DRIVE
  EXPECT_EQ(sink_.of(prop::kCurrentGear).size(), 2u);
  EXPECT_EQ(sink_.of(prop::kGearSelection).size(), 1u);
  // PERF_VEHICLE_SPEED is continuous: every valid message is delivered.
  EXPECT_EQ(sink_.of(prop::kPerfVehicleSpeed).size(), 3u);
}

// CTS-09
TEST_F(BridgeTest, EveryValueStampedWithArrivalTime) {
  Start(CommanderMap()).onTelemetry({10.0, 1}, kT0 + 123);
  EXPECT_EQ(Last(prop::kPerfVehicleSpeed).timestamp_ns, kT0 + 123);
  EXPECT_EQ(Last(prop::kCurrentGear).timestamp_ns, kT0 + 123);
  EXPECT_EQ(Last(prop::kGearSelection).timestamp_ns, kT0 + 123);
}

// CTS-12
TEST_F(BridgeTest, TelemetryStaleAfterLimit) {
  Bridge& b = Start(CommanderMap());
  b.onTelemetry({36.0, 1}, kT0);
  sink_.updates.clear();
  b.tick(kT0 + 500 * kMs);
  EXPECT_TRUE(sink_.updates.empty());
  b.tick(kT0 + 501 * kMs);
  for (int32_t p :
       {prop::kPerfVehicleSpeed, prop::kCurrentGear, prop::kGearSelection}) {
    ASSERT_EQ(sink_.of(p).size(), 1u) << p;
    EXPECT_EQ(Last(p).status, Status::kUnavailable) << p;
    EXPECT_EQ(Last(p).timestamp_ns, kT0 + 501 * kMs) << p;
    EXPECT_EQ(log_.of(p).back().reason, Reason::kStale) << p;
  }
  EXPECT_TRUE(sink_.of(prop::kParkingBrakeOn).empty());
}

// CTS-15
TEST_F(BridgeTest, ValidMessageAfterStaleIsAvailableAgain) {
  Bridge& b = Start(CommanderMap());
  b.onTelemetry({36.0, 1}, kT0);
  b.tick(kT0 + 600 * kMs);
  b.onTelemetry({18.0, 2}, kT0 + 700 * kMs);
  EXPECT_EQ(Last(prop::kPerfVehicleSpeed).status, Status::kAvailable);
  EXPECT_FLOAT_EQ(Last(prop::kPerfVehicleSpeed).float_value, 5.0f);
  EXPECT_EQ(Last(prop::kCurrentGear).status, Status::kAvailable);
  EXPECT_EQ(Last(prop::kCurrentGear).int_value, 0x0020);
  EXPECT_EQ(Last(prop::kGearSelection).status, Status::kAvailable);
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0008);
}

// CTS-16
TEST_F(BridgeTest, InvalidSpeedIsErrorOutOfRange) {
  Bridge& b = Start(Cat793fMap());
  b.onTelemetry({std::nan(""), 1}, kT0);
  EXPECT_EQ(Last(prop::kPerfVehicleSpeed).status, Status::kError);
  EXPECT_EQ(log_.of(prop::kPerfVehicleSpeed).back().reason,
            Reason::kOutOfRange);
  b.onTelemetry({80.0, 1}, kT0 + kMs);
  EXPECT_EQ(Last(prop::kPerfVehicleSpeed).status, Status::kAvailable);
  b.onTelemetry({80.1, 1}, kT0 + 2 * kMs);
  EXPECT_EQ(Last(prop::kPerfVehicleSpeed).status, Status::kError);
  b.onTelemetry({-std::numeric_limits<double>::infinity(), 1}, kT0 + 3 * kMs);
  EXPECT_EQ(Last(prop::kPerfVehicleSpeed).status, Status::kError);
}

// CTS-16, CTS-20: the limit comes from the map.
TEST_F(BridgeTest, MaxSpeedIsPerVehicle) {
  Start(CommanderMap()).onTelemetry({200.0, 5}, kT0);
  EXPECT_EQ(Last(prop::kPerfVehicleSpeed).status, Status::kAvailable);
  Start(Cat793fMap()).onTelemetry({200.0, 5}, kT0);
  EXPECT_EQ(Last(prop::kPerfVehicleSpeed).status, Status::kError);
}

TEST_F(BridgeTest, SpeedErrorDoesNotAffectGears) {
  Bridge& b = Start(CommanderMap());
  b.onTelemetry({std::nan(""), 3}, kT0);
  EXPECT_EQ(Last(prop::kCurrentGear).status, Status::kAvailable);
  EXPECT_EQ(Last(prop::kCurrentGear).int_value, 0x0040);
  EXPECT_EQ(Last(prop::kGearSelection).status, Status::kAvailable);
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0008);
}

// CTS-17
TEST_F(BridgeTest, UnknownGearIsUnavailableAndDoesNotAffectSpeed) {
  Bridge& b = Start(CommanderMap());
  b.onTelemetry({36.0, 1}, kT0 - kMs);
  b.onTelemetry({36.0, 10}, kT0);
  EXPECT_EQ(Last(prop::kCurrentGear).status, Status::kUnavailable);
  EXPECT_EQ(log_.of(prop::kCurrentGear).back().reason, Reason::kUnknownGear);
  EXPECT_EQ(Last(prop::kGearSelection).status, Status::kUnavailable);
  EXPECT_EQ(log_.of(prop::kGearSelection).back().reason, Reason::kUnknownGear);
  EXPECT_EQ(Last(prop::kPerfVehicleSpeed).status, Status::kAvailable);
  EXPECT_FLOAT_EQ(Last(prop::kPerfVehicleSpeed).float_value, 10.0f);
  b.onTelemetry({36.0, 2}, kT0 + kMs);
  b.onTelemetry({36.0, -10}, kT0 + 2 * kMs);
  EXPECT_EQ(Last(prop::kCurrentGear).status, Status::kUnavailable);
  EXPECT_EQ(log_.of(prop::kCurrentGear).back().reason, Reason::kUnknownGear);
}

// CTS-19
TEST_F(BridgeTest, EachTransitionLoggedOnceWithReason) {
  Bridge& b = Start(CommanderMap());
  b.onTelemetry({36.0, 1}, kT0);
  b.onTelemetry({36.0, 1}, kT0 + 15 * kMs);
  b.onTelemetry({std::nan(""), 1}, kT0 + 30 * kMs);
  b.onTelemetry({std::nan(""), 1}, kT0 + 45 * kMs);
  b.tick(kT0 + 600 * kMs);
  b.tick(kT0 + 650 * kMs);
  b.onTelemetry({36.0, 1}, kT0 + 700 * kMs);
  const std::vector<LogEntry> speed = log_.of(prop::kPerfVehicleSpeed);
  ASSERT_EQ(speed.size(), 4u);
  EXPECT_EQ(speed[0].status, Status::kAvailable);
  EXPECT_EQ(speed[1].status, Status::kError);
  EXPECT_EQ(speed[1].reason, Reason::kOutOfRange);
  EXPECT_EQ(speed[2].status, Status::kUnavailable);
  EXPECT_EQ(speed[2].reason, Reason::kStale);
  EXPECT_EQ(speed[3].status, Status::kAvailable);
  EXPECT_EQ(log_.of(prop::kCurrentGear).size(), 3u);  // AVAIL, STALE, AVAIL
}

// CTS-23
TEST_F(BridgeTest, InvalidMapKeepsAllFourUnavailableMapInvalid) {
  VehicleMap m = Cat793fMap();
  m.map_version = 99;
  bridge_.emplace(m, sink_, log_, kT0);
  for (int32_t p : kAllFour) {
    ASSERT_EQ(sink_.of(p).size(), 1u) << p;
    EXPECT_EQ(Last(p).status, Status::kUnavailable) << p;
    ASSERT_EQ(log_.of(p).size(), 1u) << p;
    EXPECT_EQ(log_.of(p)[0].reason, Reason::kMapInvalid) << p;
  }
  sink_.updates.clear();
  bridge_->onTelemetry({36.0, 1}, kT0 + kMs);
  bridge_->tick(kT0 + 1000 * kMs);
  EXPECT_TRUE(sink_.updates.empty());
}

// CTS-04
TEST_F(BridgeTest, LeverCodeDrivesGearSelection) {
  Bridge& b = Start(Cat793fMap());
  b.onLever({80}, kT0 + 3);  // 'P'
  EXPECT_EQ(Last(prop::kGearSelection).status, Status::kAvailable);
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0004);
  EXPECT_EQ(Last(prop::kGearSelection).timestamp_ns, kT0 + 3);
  b.onLever({68}, kT0 + kMs);  // 'D'
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0008);
}

// CTS-04, CTS-08
TEST_F(BridgeTest, RepeatedLeverCodeDoesNotRepeatEvent) {
  Bridge& b = Start(Cat793fMap());
  b.onLever({82}, kT0);
  b.onLever({82}, kT0 + 15 * kMs);
  ASSERT_EQ(sink_.of(prop::kGearSelection).size(), 1u);
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0002);
}

// CTS-04: with a lever source the telemetry gear never drives the selection.
TEST_F(BridgeTest, LeverSourceIgnoresTelemetryForSelection) {
  Bridge& b = Start(Cat793fMap());
  b.onLever({80}, kT0);
  b.onTelemetry({10.0, 2}, kT0 + kMs);
  ASSERT_EQ(sink_.of(prop::kGearSelection).size(), 1u);
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0004);
}

// CTS-14
TEST_F(BridgeTest, StaleLeverMakesSelectionUnavailableWithFreshTelemetry) {
  Bridge& b = Start(Cat793fMap());
  b.onLever({68}, kT0);
  b.onTelemetry({36.0, 1}, kT0 + 400 * kMs);
  b.tick(kT0 + 501 * kMs);
  EXPECT_EQ(Last(prop::kGearSelection).status, Status::kUnavailable);
  EXPECT_EQ(log_.of(prop::kGearSelection).back().reason, Reason::kStale);
  EXPECT_EQ(Last(prop::kPerfVehicleSpeed).status, Status::kAvailable);
  EXPECT_EQ(Last(prop::kCurrentGear).status, Status::kAvailable);
  b.onLever({78}, kT0 + 600 * kMs);  // CTS-15: back with the new value
  EXPECT_EQ(Last(prop::kGearSelection).status, Status::kAvailable);
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0001);
}

// CTS-14: lever validity is its own, not the telemetry's.
TEST_F(BridgeTest, StaleTelemetryDoesNotAffectLeverSelection) {
  Bridge& b = Start(Cat793fMap());
  b.onTelemetry({36.0, 1}, kT0);
  b.onLever({68}, kT0 + 400 * kMs);
  b.tick(kT0 + 501 * kMs);
  EXPECT_EQ(Last(prop::kPerfVehicleSpeed).status, Status::kUnavailable);
  EXPECT_EQ(Last(prop::kGearSelection).status, Status::kAvailable);
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0008);
}

// CTS-17
TEST_F(BridgeTest, UnknownLeverCodeIsUnavailableUnknownGear) {
  Bridge& b = Start(Cat793fMap());
  b.onLever({80}, kT0);
  b.onLever({83}, kT0 + kMs);  // 'S' is not in the table
  EXPECT_EQ(Last(prop::kGearSelection).status, Status::kUnavailable);
  EXPECT_EQ(log_.of(prop::kGearSelection).back().reason, Reason::kUnknownGear);
}

// CTS-05: a derived map ignores the lever.
TEST_F(BridgeTest, DerivedSourceIgnoresLever) {
  Bridge& b = Start(CommanderMap());
  b.onLever({80}, kT0);
  EXPECT_TRUE(sink_.of(prop::kGearSelection).empty());
  b.onTelemetry({0.0, 0}, kT0 + kMs);
  b.onLever({80}, kT0 + 2 * kMs);
  ASSERT_EQ(sink_.of(prop::kGearSelection).size(), 1u);
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0001);
}

// CTS-22: same telemetry, two maps, no code change.
TEST_F(BridgeTest, SameTelemetryParkOnCat793fNeutralOnCommander) {
  Bridge& cat = Start(Cat793fMap());
  cat.onTelemetry({0.0, 0}, kT0);
  cat.onLever({80}, kT0);
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0004);
  Bridge& commander = Start(CommanderMap());
  commander.onTelemetry({0.0, 0}, kT0);
  commander.onLever({80}, kT0);
  EXPECT_EQ(Last(prop::kGearSelection).int_value, 0x0001);
}

}  // namespace
}  // namespace vipee
