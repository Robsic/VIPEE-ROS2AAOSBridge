/**
 * @file camera_bridge.h
 * @brief Connects the CompressedImage topics of the map to one CameraChannel
 * each, with a LatestSlot and a thread per camera (spec CAM-01, 04, 06..08,
 * 23).
 */
#ifndef VIPEE_BRIDGE_CAMERA_BRIDGE_H_
#define VIPEE_BRIDGE_CAMERA_BRIDGE_H_

#include <functional>
#include <memory>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "vipee/camera_channel.h"
#include "vipee/frame_sink.h"
#include "vipee/vehicle_map.h"

namespace vipee {

/// @brief Creates the output device of a camera (V4l2Sink in the product, a
/// test sink in the ROS test). Called once per camera, in the constructor.
using SinkFactory =
    std::function<std::unique_ptr<FrameSink>(const CameraConfig&)>;

/**
 * @brief One subscription (SensorDataQoS, depth 1) and one thread per camera.
 *
 * The subscription callback only stamps the arrival (CLOCK_BOOTTIME ns) and
 * puts the message in the camera's slot, so a slow decode or write drops older
 * frames and never delays other cameras or the node's executor. The camera
 * thread waits up to 50 ms for a frame, hands it to the CameraChannel, then
 * calls tick() (staleness, device retry), and logs
 * "CAM <name> STATS frames=<n> p95_ms=<x> drops=<n>" every 5 s. The
 * CameraChannel is touched only by its own thread. The destructor stops the
 * subscriptions and joins the threads. The CameraLogger is called from the
 * camera threads and must be thread-safe.
 */
class CameraBridge {
 public:
  /**
   * @param node Node that owns the subscriptions; must outlive the bridge.
   * @param cameras Validated cameras (validateCameras() == kNone).
   * @param logger Log receiver; must outlive the bridge, thread-safe.
   * @param make_sink Creates the FrameSink of each camera.
   */
  CameraBridge(rclcpp::Node& node, const std::vector<CameraConfig>& cameras,
               CameraLogger& logger, const SinkFactory& make_sink);
  ~CameraBridge();
  CameraBridge(const CameraBridge&) = delete;
  CameraBridge& operator=(const CameraBridge&) = delete;

 private:
  struct Camera;
  std::vector<std::unique_ptr<Camera>> cameras_;
};

}  // namespace vipee

#endif  // VIPEE_BRIDGE_CAMERA_BRIDGE_H_
