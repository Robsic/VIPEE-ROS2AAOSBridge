#include "vipee_bridge/bridge_node.h"

#include <time.h>

#include <chrono>
#include <set>

namespace vipee {
namespace {

// Assigns the override to *out when present and of type T; otherwise leaves
// *out untouched so validateMap() reports the map.
template <typename T>
void read(const std::map<std::string, rclcpp::ParameterValue>& o,
          const std::string& key, T* out) {
  const auto it = o.find(key);
  if (it == o.end()) return;
  try {
    *out = it->second.get<T>();
  } catch (const rclcpp::ParameterTypeException&) {
  }
}

}  // namespace

int64_t boottimeNs() {
  timespec ts{};
  clock_gettime(CLOCK_BOOTTIME, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

VehicleMap mapFromOverrides(
    const std::map<std::string, rclcpp::ParameterValue>& o) {
  VehicleMap m;
  int64_t version = 0;
  read<int64_t>(o, "map_version", &version);
  m.map_version = static_cast<int>(version);
  read<std::string>(o, "vehicle", &m.vehicle);
  read<std::string>(o, "namespace", &m.ns);
  read<int64_t>(o, "stale_ms", &m.stale_ms);
  read<std::string>(o, "speed.source", &m.speed_source);
  read<double>(o, "speed.scale", &m.speed_scale);
  read<double>(o, "speed.max_abs_kmh", &m.max_abs_kmh);
  read<std::string>(o, "current_gear.source", &m.current_gear_source);
  read<std::string>(o, "gear_selection.source", &m.gear_selection_source);
  std::vector<int64_t> codes;
  read<std::vector<int64_t>>(o, "gear_selection.lever_codes", &codes);
  for (const int64_t c : codes)
    m.lever_codes.push_back(static_cast<int32_t>(c));
  read<std::vector<std::string>>(o, "gear_selection.lever_gears",
                                 &m.lever_gears);
  read<std::string>(o, "parking_brake.source", &m.parking_brake_source);

  // Property sections are the dotted top-level keys (map_version, vehicle,
  // namespace, stale_ms are plain keys). ROS adds qos_overrides.* itself.
  std::set<std::string> sections;
  for (const auto& kv : o) {
    const auto dot = kv.first.find('.');
    if (dot == std::string::npos) continue;
    const std::string section = kv.first.substr(0, dot);
    if (section != "qos_overrides") sections.insert(section);
  }
  m.sections.assign(sections.begin(), sections.end());
  return m;
}

BridgeNode::BridgeNode(const rclcpp::NodeOptions& options,
                       const std::string& socket_path, Logger& logger)
    : rclcpp::Node("vipee_bridge", options),
      map_(mapFromOverrides(
          get_node_parameters_interface()->get_parameter_overrides())),
      map_error_(validateMap(map_)),
      sink_(socket_path),
      bridge_(std::make_unique<Bridge>(map_, sink_, logger, boottimeNs())) {
  if (map_error_ != MapError::kNone) {
    RCLCPP_ERROR(get_logger(), "vehicle map refused (MapError %d)",
                 static_cast<int>(map_error_));
  } else {
    const std::string prefix = "/" + map_.ns;
    telemetry_sub_ = create_subscription<sirv_msgs::msg::SirvVehicleTelemetry>(
        prefix + "/telemetry", rclcpp::QoS(5),
        [this](const sirv_msgs::msg::SirvVehicleTelemetry& m) {
          bridge_->onTelemetry({m.speed, m.gear}, boottimeNs());
        });
    if (map_.gear_selection_source == "lever") {
      lever_sub_ = create_subscription<sirv_msgs::msg::CAT793FVehicleControl>(
          prefix + "/vehicle_control", rclcpp::QoS(1),
          [this](const sirv_msgs::msg::CAT793FVehicleControl& m) {
            bridge_->onLever({m.transmission}, boottimeNs());
          });
    }
    control_sub_ = create_subscription<sirv_msgs::msg::SirvVehicleControl>(
        "/apply_control_to_vehicle", rclcpp::QoS(2),
        [this](const sirv_msgs::msg::SirvVehicleControl& m) {
          bridge_->onControl({m.id, m.handbrake}, boottimeNs());
        });
  }
  timer_ = create_wall_timer(std::chrono::milliseconds(50), [this] {
    const int64_t now = boottimeNs();
    bridge_->tick(now);
    sink_.poll(now);
  });
}

}  // namespace vipee
