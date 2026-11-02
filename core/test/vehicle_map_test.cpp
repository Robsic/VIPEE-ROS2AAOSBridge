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
  m.map_version = 1;
  EXPECT_EQ(validateMap(m), MapError::kUnknownVersion);
  m.map_version = 3;
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

TEST(VehicleMap, MapVersionIsTwo) {
  VehicleMap m = Cat793fMap();
  m.map_version = 2;
  EXPECT_EQ(validateMap(m), MapError::kNone);
}

TEST(CameraMap, BothVehicleMapsDeclareFourValidCameras) {
  EXPECT_EQ(Cat793fMap().cameras.size(), 4u);
  EXPECT_EQ(validateCameras(Cat793fMap().cameras), CameraMapError::kNone);
  EXPECT_EQ(validateCameras(CommanderMap().cameras), CameraMapError::kNone);
}

TEST(CameraMap, NoCamerasIsValid) {
  EXPECT_EQ(validateCameras({}), CameraMapError::kNone);
}

TEST(CameraMap, EvsViewNamesFollowCarEvsManager) {
  EXPECT_EQ(evsViewFromName("FRONTVIEW"), EvsView::kFront);
  EXPECT_EQ(evsViewFromName("REARVIEW"), EvsView::kRear);
  EXPECT_EQ(evsViewFromName("LEFTVIEW"), EvsView::kLeft);
  EXPECT_EQ(evsViewFromName("RIGHTVIEW"), EvsView::kRight);
  EXPECT_EQ(evsViewFromName("SURROUNDVIEW"), EvsView::kUnknown);
  EXPECT_EQ(evsViewFromName("frontview"), EvsView::kUnknown);
  EXPECT_EQ(evsViewFromName(""), EvsView::kUnknown);
}

TEST(CameraMap, UnknownViewIsRejected) {
  VehicleMap m = CommanderMap();
  m.cameras[2].view = EvsView::kUnknown;
  EXPECT_EQ(validateCameras(m.cameras), CameraMapError::kUnknownView);
}

TEST(CameraMap, RepeatedViewIsRejected) {
  VehicleMap m = CommanderMap();
  m.cameras[1].view = EvsView::kFront;
  EXPECT_EQ(validateCameras(m.cameras), CameraMapError::kDuplicateView);
}

TEST(CameraMap, RepeatedDeviceIsRejected) {
  VehicleMap m = CommanderMap();
  m.cameras[3].device = "/dev/video10";
  EXPECT_EQ(validateCameras(m.cameras), CameraMapError::kDuplicateDevice);
}

TEST(CameraMap, MissingFieldIsRejected) {
  VehicleMap m = CommanderMap();
  m.cameras[0].name.clear();
  EXPECT_EQ(validateCameras(m.cameras), CameraMapError::kMissingField);
  m = CommanderMap();
  m.cameras[1].topic.clear();
  EXPECT_EQ(validateCameras(m.cameras), CameraMapError::kMissingField);
  m = CommanderMap();
  m.cameras[2].device.clear();
  EXPECT_EQ(validateCameras(m.cameras), CameraMapError::kMissingField);
}

TEST(CameraMap, NonPositiveSizeIsRejected) {
  VehicleMap m = CommanderMap();
  m.cameras[0].width = 0;
  EXPECT_EQ(validateCameras(m.cameras), CameraMapError::kBadSize);
  m = CommanderMap();
  m.cameras[0].height = -720;
  EXPECT_EQ(validateCameras(m.cameras), CameraMapError::kBadSize);
}

TEST(CameraMap, NonPositiveStaleMsIsRejected) {
  VehicleMap m = CommanderMap();
  m.cameras[3].stale_ms = 0;
  EXPECT_EQ(validateCameras(m.cameras), CameraMapError::kBadStaleMs);
  m.cameras[3].stale_ms = -500;
  EXPECT_EQ(validateCameras(m.cameras), CameraMapError::kBadStaleMs);
}

TEST(CameraMap, InvalidCamerasDoNotInvalidateTelemetry) {
  VehicleMap m = Cat793fMap();
  m.cameras[1].view = EvsView::kFront;
  m.cameras[2].width = 0;
  EXPECT_EQ(validateMap(m), MapError::kNone);
}

}  // namespace
}  // namespace vipee
