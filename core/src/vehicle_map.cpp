#include "vipee/vehicle_map.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace vipee {

std::optional<int32_t> gearFromName(const std::string& name) {
  if (name == "NEUTRAL") return gear::kNeutral;
  if (name == "REVERSE") return gear::kReverse;
  if (name == "PARK") return gear::kPark;
  if (name == "DRIVE") return gear::kDrive;
  for (int n = 1; n <= 9; ++n) {
    if (name == "GEAR_" + std::to_string(n)) return gear::kGear1 << (n - 1);
  }
  return std::nullopt;
}

EvsView evsViewFromName(const std::string& name) {
  if (name == "FRONTVIEW") return EvsView::kFront;
  if (name == "REARVIEW") return EvsView::kRear;
  if (name == "LEFTVIEW") return EvsView::kLeft;
  if (name == "RIGHTVIEW") return EvsView::kRight;
  return EvsView::kUnknown;
}

CameraMapError validateCameras(const std::vector<CameraConfig>& cameras) {
  std::set<EvsView> views;
  std::set<std::string> devices;
  for (const auto& c : cameras) {
    if (c.name.empty() || c.topic.empty() || c.device.empty()) {
      return CameraMapError::kMissingField;
    }
    if (c.view == EvsView::kUnknown) return CameraMapError::kUnknownView;
    if (c.width <= 0 || c.height <= 0) return CameraMapError::kBadSize;
    if (c.stale_ms <= 0) return CameraMapError::kBadStaleMs;
    if (!views.insert(c.view).second) return CameraMapError::kDuplicateView;
    if (!devices.insert(c.device).second) {
      return CameraMapError::kDuplicateDevice;
    }
  }
  return CameraMapError::kNone;
}

MapError validateMap(const VehicleMap& map) {
  if (map.map_version != kMapVersion) return MapError::kUnknownVersion;
  if (map.vehicle.empty() || map.ns.empty()) return MapError::kMissingField;

  const std::set<std::string> kFour = {"speed", "current_gear",
                                       "gear_selection", "parking_brake"};
  for (const auto& s : map.sections) {
    if (kFour.count(s) == 0) return MapError::kUnknownProperty;
  }
  if (std::set<std::string>(map.sections.begin(), map.sections.end()) !=
      kFour) {
    return MapError::kMissingField;
  }

  if (map.speed_source != "telemetry.speed" ||
      map.current_gear_source != "telemetry.gear" ||
      map.parking_brake_source != "apply_control_to_vehicle.handbrake" ||
      (map.gear_selection_source != "lever" &&
       map.gear_selection_source != "derived")) {
    return MapError::kUnknownSource;
  }
  if (map.stale_ms <= 0) return MapError::kBadStaleMs;
  if (!std::isfinite(map.max_abs_kmh) || map.max_abs_kmh <= 0.0) {
    return MapError::kBadMaxSpeed;
  }
  if (std::fabs(map.speed_scale - kKmhToMps) > 1e-6) return MapError::kBadScale;

  if (map.gear_selection_source == "lever") {
    if (map.lever_codes.empty() && map.lever_gears.empty()) {
      return MapError::kMissingField;
    }
    if (map.lever_codes.size() != map.lever_gears.size()) {
      return MapError::kLeverTableMismatch;
    }
    if (std::set<int32_t>(map.lever_codes.begin(), map.lever_codes.end())
            .size() != map.lever_codes.size()) {
      return MapError::kDuplicateLeverCode;
    }
    if (std::any_of(map.lever_gears.begin(), map.lever_gears.end(),
                    [](const std::string& g) { return !gearFromName(g); })) {
      return MapError::kUnknownLeverGear;
    }
  }
  return MapError::kNone;
}

}  // namespace vipee
