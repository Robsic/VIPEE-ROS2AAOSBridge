#include "vipee_bridge/camera_bridge.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "sensor_msgs/msg/compressed_image.hpp"
#include "vipee/latest_slot.h"
#include "vipee_bridge/bridge_node.h"

namespace vipee {
namespace {

constexpr int64_t kStatsPeriodNs = 5000000000;
constexpr std::chrono::milliseconds kTickPeriod(50);

struct Item {
  sensor_msgs::msg::CompressedImage::ConstSharedPtr msg;
  int64_t rx_ns;
};

}  // namespace

struct CameraBridge::Camera {
  Camera(const CameraConfig& c, std::unique_ptr<FrameSink> s, CameraLogger& l)
      : config(c), sink(std::move(s)), logger(l), channel(c, *sink, l) {}

  // SPEC_DEVIATION: design.md has 50 ms and 5 s timers on the node calling
  // tick() and STATS; here the camera thread does both.
  // Reason: CameraChannel is not thread-safe, and its own thread is the only
  // one that touches it.
  void run() {
    int64_t next_stats = boottimeNs() + kStatsPeriodNs;
    while (running) {
      const auto item = slot.take(kTickPeriod);
      if (!running) break;
      if (item) {
        const auto& m = *item->msg;
        channel.onFrame(m.format, m.data.data(), m.data.size(), item->rx_ns,
                        boottimeNs());
      }
      const int64_t now = boottimeNs();
      channel.tick(now);
      if (now >= next_stats) {
        next_stats = now + kStatsPeriodNs;
        const CameraStats s = channel.takeStats();
        const int64_t drops =
            s.format_unsupported + s.decode_error + s.frame_size_mismatch;
        logger.log("CAM " + config.name +
                   " STATS frames=" + std::to_string(s.frames) + " p95_ms=" +
                   std::to_string(static_cast<double>(s.p95_latency_ns) / 1e6) +
                   " drops=" + std::to_string(drops));
      }
    }
  }

  CameraConfig config;
  std::unique_ptr<FrameSink> sink;
  CameraLogger& logger;
  CameraChannel channel;
  LatestSlot<Item> slot;
  rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr sub;
  std::atomic<bool> running{true};
  std::thread thread;
};

CameraBridge::CameraBridge(rclcpp::Node& node,
                           const std::vector<CameraConfig>& cameras,
                           CameraLogger& logger, const SinkFactory& make_sink) {
  for (const CameraConfig& config : cameras) {
    auto cam = std::make_unique<Camera>(config, make_sink(config), logger);
    Camera* raw = cam.get();
    cam->sub = node.create_subscription<sensor_msgs::msg::CompressedImage>(
        config.topic, rclcpp::SensorDataQoS().keep_last(1),
        [raw](sensor_msgs::msg::CompressedImage::ConstSharedPtr m) {
          raw->slot.put({std::move(m), boottimeNs()});
        });
    cam->thread = std::thread([raw] { raw->run(); });
    cameras_.push_back(std::move(cam));
  }
}

CameraBridge::~CameraBridge() {
  for (auto& cam : cameras_) {
    cam->sub.reset();
    cam->running = false;
    cam->slot.close();
  }
  for (auto& cam : cameras_) cam->thread.join();
}

}  // namespace vipee
