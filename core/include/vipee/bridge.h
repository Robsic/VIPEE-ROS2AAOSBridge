/**
 * @file bridge.h
 * @brief Bridge core: simulator messages ➔ value and status of the four VHAL
 * properties, with staleness and fail-loud (spec CTS-01..CTS-23).
 *
 * No ROS and no Android: the ROS node copies message fields into the *In
 * structs and implements PropertySink and Logger. Not thread-safe; callbacks
 * and tick() must run on one thread (SingleThreadedExecutor).
 */
#ifndef VIPEE_BRIDGE_H_
#define VIPEE_BRIDGE_H_

#include <cstdint>

#include "vipee/translate.h"
#include "vipee/vehicle_map.h"

namespace vipee {

/// @brief One property value as delivered to the VHAL.
struct PropertyUpdate {
  int32_t prop_id = 0;  ///< VehicleProperty id (see vipee::prop).
  Status status = Status::kUnavailable;  ///< VehiclePropertyStatus.
  int32_t int_value = 0;     ///< VehicleGear bit, or 0/1 for PARKING_BRAKE_ON.
  float float_value = 0.0f;  ///< PERF_VEHICLE_SPEED in m/s, signed.
  int64_t timestamp_ns = 0;  ///< CLOCK_BOOTTIME ns at arrival of the source
                             ///< message (or of the tick that changed status).
};

/// @brief Receiver of property updates (socket to the VHAL, or a test fake).
class PropertySink {
 public:
  virtual ~PropertySink() = default;
  /// @brief Delivers one update. Must not block.
  virtual void publish(const PropertyUpdate& update) = 0;
};

/// @brief Receiver of status transitions (logcat tag vipee_bridge).
class Logger {
 public:
  virtual ~Logger() = default;
  /**
   * @brief Called once per change of a property's status or reason.
   * @param prop VehicleProperty id.
   * @param status New status.
   * @param reason Named reason (kNone when AVAILABLE).
   */
  virtual void transition(int32_t prop, Status status, Reason reason) = 0;
};

/// @brief Fields of SirvVehicleTelemetry on /<ns>/telemetry.
struct TelemetryIn {
  double speed_kmh = 0.0;  ///< Signed km/h, negative moving backwards.
  int32_t gear = 0;        ///< Engaged gear: < 0 reverse, 0 neutral, n > 0.
};

/// @brief Fields of CAT793FVehicleControl on /<ns>/vehicle_control.
struct LeverIn {
  int32_t transmission = 0;  ///< Lever ASCII code (int8), e.g. 80 'P'.
};

/**
 * @brief State machine of the four properties of this slice.
 *
 * ON_CHANGE properties (GEAR_SELECTION, CURRENT_GEAR, PARKING_BRAKE_ON) are
 * published only when value or status changes; PERF_VEHICLE_SPEED is
 * published on every valid message.
 */
class Bridge {
 public:
  /**
   * @brief Publishes and logs the initial state of the four properties:
   * UNAVAILABLE/NO_DATA, or UNAVAILABLE/MAP_INVALID if validateMap() fails
   * (then every later call is ignored).
   * @param map Vehicle map (copied).
   * @param sink Update receiver; must outlive the bridge.
   * @param logger Transition receiver; must outlive the bridge.
   * @param now_ns CLOCK_BOOTTIME ns, timestamp of the initial records.
   */
  Bridge(const VehicleMap& map, PropertySink& sink, Logger& logger,
         int64_t now_ns);

  /**
   * @brief Handles one telemetry message: PERF_VEHICLE_SPEED, CURRENT_GEAR
   * and, when the map derives it, GEAR_SELECTION.
   * @param in Copied message fields.
   * @param now_ns CLOCK_BOOTTIME ns at arrival.
   */
  void onTelemetry(const TelemetryIn& in, int64_t now_ns);

  /**
   * @brief Handles one lever message. Drives GEAR_SELECTION, with its own
   * staleness, only when the map's gear_selection_source is "lever";
   * ignored otherwise.
   * @param in Copied message fields.
   * @param now_ns CLOCK_BOOTTIME ns at arrival.
   */
  void onLever(const LeverIn& in, int64_t now_ns);

  /**
   * @brief Applies the staleness limit: a source silent for more than
   * map.stale_ms makes its properties UNAVAILABLE/STALE.
   * @param now_ns CLOCK_BOOTTIME ns.
   */
  void tick(int64_t now_ns);

 private:
  struct Prop {
    int32_t id;
    bool continuous;
    Status status = Status::kUnavailable;
    Reason reason = Reason::kNoData;
    int32_t int_value = 0;
    float float_value = 0.0f;
  };

  void set(Prop& p, Status status, Reason reason, int32_t int_value,
           float float_value, int64_t now_ns);
  void setGear(Prop& p, const GearResult& g, int64_t now_ns);
  void setUnavailable(Prop& p, Reason reason, int64_t now_ns);
  bool isStale(int64_t rx_ns, int64_t now_ns) const;

  VehicleMap map_;
  PropertySink& sink_;
  Logger& logger_;
  bool valid_;
  bool derived_selection_;
  Prop speed_{prop::kPerfVehicleSpeed, true};
  Prop current_gear_{prop::kCurrentGear, false};
  Prop selection_{prop::kGearSelection, false};
  Prop brake_{prop::kParkingBrakeOn, false};
  int64_t telemetry_rx_ns_ = -1;  ///< Last telemetry arrival, -1 if none.
  int64_t lever_rx_ns_ = -1;      ///< Last lever arrival, -1 if none.
};

}  // namespace vipee

#endif  // VIPEE_BRIDGE_H_
