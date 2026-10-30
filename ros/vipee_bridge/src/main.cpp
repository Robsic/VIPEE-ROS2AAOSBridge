#include <cstdlib>
#include <exception>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "vipee_bridge/bridge_node.h"

#ifdef __ANDROID__
#include <android/log.h>
#else
#include <cstdio>
#endif

namespace {

const char* statusName(vipee::Status s) {
  switch (s) {
    case vipee::Status::kAvailable:
      return "AVAILABLE";
    case vipee::Status::kUnavailable:
      return "UNAVAILABLE";
    case vipee::Status::kError:
      return "ERROR";
  }
  return "?";
}

const char* reasonName(vipee::Reason r) {
  switch (r) {
    case vipee::Reason::kNone:
      return "OK";
    case vipee::Reason::kNoData:
      return "NO_DATA";
    case vipee::Reason::kStale:
      return "STALE";
    case vipee::Reason::kOutOfRange:
      return "OUT_OF_RANGE";
    case vipee::Reason::kUnknownGear:
      return "UNKNOWN_GEAR";
    case vipee::Reason::kAmbiguousSource:
      return "AMBIGUOUS_SOURCE";
    case vipee::Reason::kMapInvalid:
      return "MAP_INVALID";
  }
  return "?";
}

// logcat tag vipee_bridge on Android, stderr on the host.
class PlatformLogger : public vipee::Logger {
 public:
  void transition(int32_t prop, vipee::Status status,
                  vipee::Reason reason) override {
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "vipee_bridge", "prop 0x%08x %s %s",
                        prop, statusName(status), reasonName(reason));
#else
    std::fprintf(stderr, "vipee_bridge: prop 0x%08x %s %s\n", prop,
                 statusName(status), reasonName(reason));
#endif
  }
};

}  // namespace

int main(int argc, char** argv) {
  try {
    rclcpp::init(argc, argv);
    const char* path = std::getenv("VIPEE_VHAL_SOCKET");
    PlatformLogger logger;
    auto node = std::make_shared<vipee::BridgeNode>(
        rclcpp::NodeOptions(), path ? path : "/dev/socket/vipee_vhal", logger);
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.spin();
    rclcpp::shutdown();
  } catch (const std::exception& e) {
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_ERROR, "vipee_bridge", "fatal: %s",
                        e.what());
#else
    std::fprintf(stderr, "vipee_bridge: fatal: %s\n", e.what());
#endif
    return 1;
  }
  return 0;
}
