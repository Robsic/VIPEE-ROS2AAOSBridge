/**
 * @file translate.h
 * @brief Pure translations from simulator fields to VHAL values (spec
 * CTS-01..CTS-06, CTS-16, CTS-17).
 */
#ifndef VIPEE_TRANSLATE_H_
#define VIPEE_TRANSLATE_H_

#include <cstdint>

#include "vipee/vehicle_map.h"

namespace vipee {

/// @brief VehiclePropertyStatus values carried to the VHAL.
enum class Status : int32_t {
  kAvailable = 0,    ///< AVAILABLE.
  kUnavailable = 1,  ///< UNAVAILABLE.
  kError = 2,        ///< ERROR.
};

/// @brief Named reason of a property's status (logged on transitions).
enum class Reason {
  kNone,             ///< Valid value.
  kNoData,           ///< NO_DATA: no valid message since start.
  kStale,            ///< STALE: source silent for more than stale_ms.
  kOutOfRange,       ///< OUT_OF_RANGE: speed NaN, infinite or above max.
  kUnknownGear,      ///< UNKNOWN_GEAR: gear or lever code not in the table.
  kAmbiguousSource,  ///< AMBIGUOUS_SOURCE: two control ids within stale_ms.
  kMapInvalid,       ///< MAP_INVALID: the vehicle map was refused.
};

/// @brief Speed translation result. Valid iff reason == Reason::kNone.
struct SpeedResult {
  float mps = 0.0f;  ///< Signed speed in m/s (negative moving backwards).
  Reason reason = Reason::kNone;  ///< kNone or kOutOfRange.
};

/// @brief Gear translation result. Valid iff reason == Reason::kNone.
struct GearResult {
  int32_t gear = gear::kUnknown;  ///< VehicleGear bit.
  Reason reason = Reason::kNone;  ///< kNone or kUnknownGear.
};

/**
 * @brief SirvVehicleTelemetry.speed (km/h, signed) ➔ PERF_VEHICLE_SPEED.
 * @param kmh Signed speed in km/h; negative when moving backwards.
 * @param map Validated map: speed_scale (km/h ➔ m/s) and max_abs_kmh
 * (inclusive |speed| limit in km/h).
 * @return m/s with the input sign (-0.0 becomes 0.0), or kOutOfRange for NaN,
 * infinity or |kmh| > max_abs_kmh.
 */
SpeedResult speedKmhToMps(double kmh, const VehicleMap& map);

/**
 * @brief Engaged gear (SirvVehicleTelemetry.gear) ➔ CURRENT_GEAR.
 * @param engaged < 0 reverse, 0 neutral, n > 0 forward gear n.
 * @return GEAR_REVERSE, GEAR_NEUTRAL or GEAR_n; kUnknownGear outside -9..9.
 */
GearResult currentGearFromEngaged(int32_t engaged);

/**
 * @brief Engaged gear ➔ GEAR_SELECTION for vehicles without a lever source.
 * @param engaged < 0 reverse, 0 neutral, n > 0 forward gear n.
 * @return GEAR_REVERSE, GEAR_NEUTRAL or GEAR_DRIVE, never GEAR_PARK;
 * kUnknownGear outside -9..9.
 */
GearResult derivedSelectionFromEngaged(int32_t engaged);

/**
 * @brief Lever code (CAT793FVehicleControl.transmission, ASCII) ➔
 * GEAR_SELECTION through the map's lever table.
 * @param code Lever ASCII code (e.g. 80 'P').
 * @param map Validated map with gear_selection_source "lever".
 * @return The table's gear, or kUnknownGear when the code is not in it.
 */
GearResult selectionFromLever(int32_t code, const VehicleMap& map);

}  // namespace vipee

#endif  // VIPEE_TRANSLATE_H_
