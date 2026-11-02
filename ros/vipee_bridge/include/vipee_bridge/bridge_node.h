/**
 * @file bridge_node.h
 * @brief ROS 2 node that feeds the bridge core from the simulator topics and
 * the SocketSink (AD-012, AD-013).
 */
#ifndef VIPEE_BRIDGE_BRIDGE_NODE_H_
#define VIPEE_BRIDGE_BRIDGE_NODE_H_

#include <cstdint>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "sirv_msgs/msg/cat793_f_vehicle_control.hpp"
#include "sirv_msgs/msg/sirv_vehicle_control.hpp"
#include "sirv_msgs/msg/sirv_vehicle_telemetry.hpp"
#include "vipee/bridge.h"
#include "vipee/camera_channel.h"
#include "vipee/vehicle_map.h"
#include "vipee_bridge/camera_bridge.h"
#include "vipee_bridge/socket_sink.h"

namespace vipee {

/**
 * @brief Reads a VehicleMap from the parameter overrides of a node.
 *
 * Parameters live under the node name (`vipee_bridge: ros__parameters:`),
 * with the layout of the maps/ YAML files. A missing or wrongly typed parameter
 * leaves its field at the "missing" value, so validateMap() refuses the map;
 * this never throws.
 *
 * @param overrides Parameter overrides, flat names such as "speed.source".
 * @return The map; `sections` lists the top-level keys that hold properties
 * (not `cameras`). `cameras` holds one entry per `cameras.<name>` key, in name
 * order, with `evs`, `topic`, `device`, `width`, `height` and `stale_ms`; a
 * missing or mistyped field stays at the "missing" value for
 * validateCameras().
 */
VehicleMap mapFromOverrides(
    const std::map<std::string, rclcpp::ParameterValue>& overrides);

/**
 * @brief Node `vipee_bridge`: subscribes to /<ns>/telemetry,
 * /<ns>/vehicle_control (only when the map takes GEAR_SELECTION from the
 * lever) and /apply_control_to_vehicle, and runs Bridge::tick plus
 * SocketSink::poll every 50 ms. With an invalid map it subscribes to nothing
 * and only reports the four properties UNAVAILABLE/MAP_INVALID; it stays
 * alive. An invalid `cameras` section logs `CAM MAP_INVALID <error>` and
 * leaves the telemetry untouched; a valid non-empty one starts a
 * CameraBridge (one thread per camera, outside the executor). Timestamps are
 * CLOCK_BOOTTIME ns. Use with a single-threaded executor.
 */
class BridgeNode : public rclcpp::Node {
 public:
  /**
   * @brief Builds the node and the bridge.
   * @param options Node options carrying the map parameters.
   * @param socket_path Unix socket of the VHAL.
   * @param logger Receiver of status transitions; must outlive the node.
   * @param camera_logger Receiver of the CAM lines; must outlive the node and
   * be thread-safe (the camera threads call it).
   * @param make_sink Creates the sink of each camera; empty means V4l2Sink on
   * the device of the map. The ROS test passes a test sink.
   */
  BridgeNode(const rclcpp::NodeOptions& options, const std::string& socket_path,
             Logger& logger, CameraLogger& camera_logger,
             SinkFactory make_sink = {});

  /// @brief Result of validating the map this node loaded.
  MapError mapError() const { return map_error_; }

  /// @brief Result of validating the cameras section of the map.
  CameraMapError cameraMapError() const { return camera_error_; }

  /// @brief The map this node loaded.
  const VehicleMap& map() const { return map_; }

 private:
  VehicleMap map_;
  MapError map_error_;
  CameraMapError camera_error_;
  SocketSink sink_;
  std::unique_ptr<Bridge> bridge_;
  std::unique_ptr<CameraBridge> camera_bridge_;
  rclcpp::Subscription<sirv_msgs::msg::SirvVehicleTelemetry>::SharedPtr
      telemetry_sub_;
  rclcpp::Subscription<sirv_msgs::msg::CAT793FVehicleControl>::SharedPtr
      lever_sub_;
  rclcpp::Subscription<sirv_msgs::msg::SirvVehicleControl>::SharedPtr
      control_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

/// @brief CLOCK_BOOTTIME in ns (same base as elapsedRealtimeNano()).
int64_t boottimeNs();

}  // namespace vipee

#endif  // VIPEE_BRIDGE_BRIDGE_NODE_H_
