#ifndef VIPEE_TEST_MAPS_H_
#define VIPEE_TEST_MAPS_H_

#include "vipee/vehicle_map.h"

namespace vipee {

// Equivalent to maps/cat793f.yaml in design.md.
inline VehicleMap Cat793fMap() {
  VehicleMap m;
  m.map_version = 1;
  m.vehicle = "cat793f";
  m.ns = "cat793f";
  m.stale_ms = 500;
  m.sections = {"speed", "current_gear", "gear_selection", "parking_brake"};
  m.speed_source = "telemetry.speed";
  m.speed_scale = 0.277777778;
  m.max_abs_kmh = 80.0;
  m.current_gear_source = "telemetry.gear";
  m.gear_selection_source = "lever";
  m.lever_codes = {80, 82, 78, 68, 49, 50};
  m.lever_gears = {"PARK", "REVERSE", "NEUTRAL", "DRIVE", "GEAR_1", "GEAR_2"};
  m.parking_brake_source = "apply_control_to_vehicle.handbrake";
  return m;
}

// Equivalent to maps/commander.yaml in design.md.
inline VehicleMap CommanderMap() {
  VehicleMap m = Cat793fMap();
  m.vehicle = "commander";
  m.ns = "commander";
  m.max_abs_kmh = 220.0;
  m.gear_selection_source = "derived";
  m.lever_codes.clear();
  m.lever_gears.clear();
  return m;
}

}  // namespace vipee

#endif  // VIPEE_TEST_MAPS_H_
