/**
 * @file vehicle_map.h
 * @brief Per-vehicle signal map (ROS2 topic/field ➔ VHAL property) and its
 * validation.
 *
 * Everything that varies per vehicle lives here (spec CTS-20): map version,
 * vehicle namespace, source of each property, unit scale, maximum speed,
 * staleness limit, the GEAR_SELECTION source with its lever table and the
 * cameras (spec CAM-16).
 */
#ifndef VIPEE_VEHICLE_MAP_H_
#define VIPEE_VEHICLE_MAP_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vipee {

/// @brief VehicleProperty ids of this slice (AOSP 16 VehicleProperty.aidl).
namespace prop {
constexpr int32_t kPerfVehicleSpeed = 0x11600207;  ///< FLOAT, m/s, CONTINUOUS.
constexpr int32_t kGearSelection = 0x11400400;     ///< INT32 VehicleGear.
constexpr int32_t kCurrentGear = 0x11400401;       ///< INT32 VehicleGear.
constexpr int32_t kParkingBrakeOn = 0x11200402;    ///< BOOLEAN.
}  // namespace prop

/// @brief VehicleGear bits (AOSP 16 VehicleGear.aidl).
namespace gear {
constexpr int32_t kUnknown = 0x0000;  ///< GEAR_UNKNOWN.
constexpr int32_t kNeutral = 0x0001;  ///< GEAR_NEUTRAL.
constexpr int32_t kReverse = 0x0002;  ///< GEAR_REVERSE.
constexpr int32_t kPark = 0x0004;     ///< GEAR_PARK.
constexpr int32_t kDrive = 0x0008;    ///< GEAR_DRIVE.
constexpr int32_t kGear1 = 0x0010;    ///< GEAR_1; GEAR_n = kGear1 << (n - 1).
}  // namespace gear

/// @brief Only map version this build understands.
constexpr int kMapVersion = 2;

/// @brief Only valid speed scale: km/h ➔ m/s (1 / 3.6).
constexpr double kKmhToMps = 1.0 / 3.6;

/// @brief EVS camera type a camera feeds (CarEvsManager SERVICE_TYPE_*).
enum class EvsView {
  kUnknown,  ///< Any name evsViewFromName() rejects.
  kFront,    ///< FRONTVIEW.
  kRear,     ///< REARVIEW.
  kLeft,     ///< LEFTVIEW.
  kRight,    ///< RIGHTVIEW.
};

/**
 * @brief One camera of the map (YAML section cameras.<name>).
 *
 * Strings left empty and numbers left at 0 mean "missing" and fail
 * validateCameras().
 */
struct CameraConfig {
  std::string name;  ///< Position key, e.g. "front"; used in the CAM logs.
  EvsView view = EvsView::kUnknown;  ///< EVS type; unique within the map.
  std::string topic;     ///< sensor_msgs/CompressedImage topic (JPEG).
  std::string device;    ///< v4l2loopback output, e.g. /dev/video40; unique.
  int width = 0;         ///< Frame width in px, > 0; other sizes are dropped.
  int height = 0;        ///< Frame height in px, > 0.
  int64_t stale_ms = 0;  ///< ms without a valid frame before STALE, > 0.
};

/**
 * @brief One vehicle's map, as read from the ROS parameter YAML.
 *
 * Strings left empty and numbers left at 0 mean "missing" and fail
 * validateMap().
 */
struct VehicleMap {
  int map_version = 0;  ///< Must equal kMapVersion.
  std::string vehicle;  ///< Vehicle name (e.g. "cat793f").
  std::string ns;  ///< ROS namespace: /<ns>/telemetry, /<ns>/vehicle_control.
  int64_t stale_ms = 0;  ///< Staleness limit in ms, > 0, per source topic.
  /// Property sections declared by the map (YAML keys). Exactly "speed",
  /// "current_gear", "gear_selection" and "parking_brake".
  std::vector<std::string> sections;
  std::string speed_source;  ///< Must be "telemetry.speed" (km/h, signed).
  double speed_scale = 0.0;  ///< km/h ➔ m/s factor; must be kKmhToMps.
  double max_abs_kmh = 0.0;  ///< Inclusive |speed| limit in km/h, > 0.
  std::string current_gear_source;  ///< Must be "telemetry.gear".
  /// "lever" (CAT793FVehicleControl.transmission on /<ns>/vehicle_control)
  /// or "derived" (from the engaged gear).
  std::string gear_selection_source;
  /// Lever ASCII codes (int8 transmission); lever_gears[i] is the gear of
  /// lever_codes[i]. Required and same size when the source is "lever".
  std::vector<int32_t> lever_codes;
  /// VehicleGear names without the GEAR_ prefix for named gears: PARK,
  /// REVERSE, NEUTRAL, DRIVE, GEAR_1..GEAR_9.
  std::vector<std::string> lever_gears;
  /// Must be "apply_control_to_vehicle.handbrake" (SirvVehicleControl).
  std::string parking_brake_source;
  /// Cameras of the vehicle; empty means telemetry only (spec CAM-18).
  /// Checked by validateCameras(), never by validateMap().
  std::vector<CameraConfig> cameras;
};

/// @brief Named reasons a map is refused (logged as MAP_INVALID).
enum class MapError {
  kNone,             ///< Valid.
  kUnknownVersion,   ///< map_version != kMapVersion.
  kMissingField,     ///< Empty name/namespace, missing section or lever table.
  kUnknownProperty,  ///< A section maps a property outside the four.
  kUnknownSource,    ///< A source this build does not read.
  kBadStaleMs,       ///< stale_ms <= 0.
  kBadMaxSpeed,      ///< max_abs_kmh <= 0 or not finite.
  kBadScale,         ///< speed_scale is not km/h ➔ m/s.
  kLeverTableMismatch,  ///< lever_codes and lever_gears differ in size.
  kDuplicateLeverCode,  ///< The same lever code appears twice.
  kUnknownLeverGear,    ///< lever_gears holds a name gearFromName() rejects.
};

/// @brief Named reasons a camera section is refused (CAM MAP_INVALID).
enum class CameraMapError {
  kNone,             ///< Valid (an empty list is valid).
  kUnknownView,      ///< A camera has EvsView::kUnknown.
  kDuplicateView,    ///< Two cameras feed the same EVS type.
  kDuplicateDevice,  ///< Two cameras write the same device.
  kMissingField,     ///< Empty name, topic or device.
  kBadSize,          ///< width or height <= 0.
  kBadStaleMs,       ///< stale_ms <= 0.
};

/**
 * @brief EvsView of an EVS type name.
 * @param name FRONTVIEW, REARVIEW, LEFTVIEW or RIGHTVIEW (case-sensitive).
 * @return The view, or EvsView::kUnknown for any other name.
 */
EvsView evsViewFromName(const std::string& name);

/**
 * @brief Checks the camera section before any camera opens (spec CAM-17).
 *
 * Independent of validateMap(): an invalid camera section opens no camera
 * and leaves the telemetry as validateMap() decides.
 * @param cameras Cameras read from the vehicle's YAML.
 * @return CameraMapError::kNone, or the first reason found.
 */
CameraMapError validateCameras(const std::vector<CameraConfig>& cameras);

/**
 * @brief VehicleGear value of a gear name.
 * @param name PARK, REVERSE, NEUTRAL, DRIVE or GEAR_1..GEAR_9.
 * @return The VehicleGear bit, or nullopt for any other name (incl. UNKNOWN).
 */
std::optional<int32_t> gearFromName(const std::string& name);

/**
 * @brief Checks a map before the bridge uses it (spec CTS-23).
 * @param map Map read from the vehicle's YAML.
 * @return MapError::kNone, or the first reason found.
 */
MapError validateMap(const VehicleMap& map);

}  // namespace vipee

#endif  // VIPEE_VEHICLE_MAP_H_
