#include "vipee/translate.h"

#include <cmath>
#include <cstddef>

namespace vipee {

namespace {
constexpr int32_t kMaxGear = 9;
}  // namespace

SpeedResult speedKmhToMps(double kmh, const VehicleMap& map) {
  if (!std::isfinite(kmh) || std::fabs(kmh) > map.max_abs_kmh) {
    return {0.0f, Reason::kOutOfRange};
  }
  const auto mps = static_cast<float>(kmh * map.speed_scale);
  return {mps == 0.0f ? 0.0f : mps, Reason::kNone};
}

GearResult currentGearFromEngaged(int32_t engaged) {
  if (engaged < -kMaxGear || engaged > kMaxGear) {
    return {gear::kUnknown, Reason::kUnknownGear};
  }
  if (engaged < 0) return {gear::kReverse, Reason::kNone};
  if (engaged == 0) return {gear::kNeutral, Reason::kNone};
  return {gear::kGear1 << (engaged - 1), Reason::kNone};
}

GearResult derivedSelectionFromEngaged(int32_t engaged) {
  GearResult r = currentGearFromEngaged(engaged);
  if (r.reason == Reason::kNone && engaged > 0) r.gear = gear::kDrive;
  return r;
}

GearResult selectionFromLever(int32_t code, const VehicleMap& map) {
  for (size_t i = 0; i < map.lever_codes.size(); ++i) {
    if (map.lever_codes[i] != code) continue;
    if (auto g = gearFromName(map.lever_gears[i])) return {*g, Reason::kNone};
  }
  return {gear::kUnknown, Reason::kUnknownGear};
}

}  // namespace vipee
