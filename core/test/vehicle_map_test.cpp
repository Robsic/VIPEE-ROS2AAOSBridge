#include "vipee/vehicle_map.h"

#include <gtest/gtest.h>

#include "test_maps.h"

namespace vipee {
namespace {

TEST(VehicleMap, Cat793fMapIsValid) {
  EXPECT_EQ(validateMap(Cat793fMap()), MapError::kNone);
}

TEST(VehicleMap, CommanderMapIsValid) {
  EXPECT_EQ(validateMap(CommanderMap()), MapError::kNone);
}

TEST(VehicleMap, UnknownVersionIsRejected) {
  VehicleMap m = Cat793fMap();
  m.map_version = 2;
  EXPECT_EQ(validateMap(m), MapError::kUnknownVersion);
  m.map_version = 0;
  EXPECT_EQ(validateMap(m), MapError::kUnknownVersion);
}

TEST(VehicleMap, NonPositiveStaleMsIsRejected) {
  VehicleMap m = Cat793fMap();
  m.stale_ms = 0;
  EXPECT_EQ(validateMap(m), MapError::kBadStaleMs);
  m.stale_ms = -500;
  EXPECT_EQ(validateMap(m), MapError::kBadStaleMs);
}

TEST(VehicleMap, NonPositiveMaxSpeedIsRejected) {
  VehicleMap m = CommanderMap();
  m.max_abs_kmh = 0.0;
  EXPECT_EQ(validateMap(m), MapError::kBadMaxSpeed);
  m.max_abs_kmh = -80.0;
  EXPECT_EQ(validateMap(m), MapError::kBadMaxSpeed);
}

TEST(VehicleMap, ScaleOtherThanKmhToMpsIsRejected) {
  VehicleMap m = Cat793fMap();
  m.speed_scale = 1.0;
  EXPECT_EQ(validateMap(m), MapError::kBadScale);
}

TEST(VehicleMap, LeverTablesWithDifferentSizesAreRejected) {
  VehicleMap m = Cat793fMap();
  m.lever_gears.pop_back();
  EXPECT_EQ(validateMap(m), MapError::kLeverTableMismatch);
}

TEST(VehicleMap, EmptyLeverTableIsRejectedForLeverSource) {
  VehicleMap m = Cat793fMap();
  m.lever_codes.clear();
  m.lever_gears.clear();
  EXPECT_EQ(validateMap(m), MapError::kMissingField);
}

TEST(VehicleMap, RepeatedLeverCodeIsRejected) {
  VehicleMap m = Cat793fMap();
  m.lever_codes[1] = 80;
  EXPECT_EQ(validateMap(m), MapError::kDuplicateLeverCode);
}

TEST(VehicleMap, UnknownLeverGearIsRejected) {
  VehicleMap m = Cat793fMap();
  m.lever_gears[0] = "PARKING";
  EXPECT_EQ(validateMap(m), MapError::kUnknownLeverGear);
  m.lever_gears[0] = "UNKNOWN";
  EXPECT_EQ(validateMap(m), MapError::kUnknownLeverGear);
}

TEST(VehicleMap, MissingRequiredFieldIsRejected) {
  VehicleMap m = Cat793fMap();
  m.ns.clear();
  EXPECT_EQ(validateMap(m), MapError::kMissingField);
  m = Cat793fMap();
  m.vehicle.clear();
  EXPECT_EQ(validateMap(m), MapError::kMissingField);
  m = Cat793fMap();
  m.sections = {"speed", "current_gear", "gear_selection"};
  EXPECT_EQ(validateMap(m), MapError::kMissingField);
}

TEST(VehicleMap, PropertyOutsideTheFourIsRejected) {
  VehicleMap m = Cat793fMap();
  m.sections.push_back("door_pos");
  EXPECT_EQ(validateMap(m), MapError::kUnknownProperty);
}

TEST(VehicleMap, UnknownSourceIsRejected) {
  VehicleMap m = Cat793fMap();
  m.gear_selection_source = "pedal";
  EXPECT_EQ(validateMap(m), MapError::kUnknownSource);
  m = Cat793fMap();
  m.speed_source = "telemetry.rpm";
  EXPECT_EQ(validateMap(m), MapError::kUnknownSource);
  m = Cat793fMap();
  m.current_gear_source = "";
  EXPECT_EQ(validateMap(m), MapError::kUnknownSource);
  m = Cat793fMap();
  m.parking_brake_source = "telemetry.handbrake";
  EXPECT_EQ(validateMap(m), MapError::kUnknownSource);
}

TEST(VehicleMap, GearNamesFollowVehicleGearAidl) {
  EXPECT_EQ(gearFromName("NEUTRAL"), 0x0001);
  EXPECT_EQ(gearFromName("REVERSE"), 0x0002);
  EXPECT_EQ(gearFromName("PARK"), 0x0004);
  EXPECT_EQ(gearFromName("DRIVE"), 0x0008);
  EXPECT_EQ(gearFromName("GEAR_1"), 0x0010);
  EXPECT_EQ(gearFromName("GEAR_2"), 0x0020);
  EXPECT_EQ(gearFromName("GEAR_9"), 0x1000);
  EXPECT_EQ(gearFromName("GEAR_10"), std::nullopt);
}

}  // namespace
}  // namespace vipee
