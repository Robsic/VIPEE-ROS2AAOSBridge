#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "vipee/dds_peer.h"
#include "vipee_bridge/bridge_node.h"

#ifdef __ANDROID__
#include <android/log.h>
#include <sys/system_properties.h>
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

// CAM lines (built by the core) under the same tag.
class PlatformCameraLogger : public vipee::CameraLogger {
 public:
  void log(const std::string& line) override {
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "vipee_bridge", "%s", line.c_str());
#else
    std::fprintf(stderr, "vipee_bridge: %s\n", line.c_str());
#endif
  }
};

void logError(const char* what) {
#ifdef __ANDROID__
  __android_log_print(ANDROID_LOG_ERROR, "vipee_bridge", "%s", what);
#else
  std::fprintf(stderr, "vipee_bridge: %s\n", what);
#endif
}

// CTS-23: an unreadable map file must not crash-loop the service. rcl refuses
// it inside init; init again without "--params-file <path>": the node then
// has no map, refuses it and reports MAP_INVALID on the four properties.
void initRclcpp(int argc, char** argv) {
  try {
    rclcpp::init(argc, argv);
    return;
  } catch (const std::exception& e) {
    logError(e.what());
  }
  std::vector<char*> args;
  for (int i = 0; i < argc; ++i) {
    if (std::strcmp(argv[i], "--params-file") == 0) {
      ++i;  // also skip the path
      continue;
    }
    args.push_back(argv[i]);
  }
  rclcpp::init(static_cast<int>(args.size()), args.data());
}

#ifdef __ANDROID__
// RPI-05/06: when the CycloneDDS XML in use has ${VIPEE_HOST}, export it from
// persist.vendor.vipee.host before rclcpp::init. On a named error, log the
// code and block forever: no DDS, no exit (init would restart-loop).
void applyDdsPeer() {
  const char* uri = std::getenv("CYCLONEDDS_URI");
  if (uri == nullptr) return;
  std::string path = uri;
  if (path.rfind("file://", 0) == 0) path.erase(0, 7);
  std::ifstream in(path);
  std::stringstream xml;
  xml << in.rdbuf();
  char host[PROP_VALUE_MAX] = "";
  __system_property_get("persist.vendor.vipee.host", host);
  const auto d = vipee::DecideDdsPeer(
      xml.str().find("${VIPEE_HOST}") != std::string::npos, host);
  if (d.start) {
    if (d.vipee_host) setenv("VIPEE_HOST", d.vipee_host->c_str(), 1);
    return;
  }
  logError(vipee::DdsPeerErrorName(d.error));
  for (;;) pause();
}
#endif

}  // namespace

int main(int argc, char** argv) {
  try {
#ifdef __ANDROID__
    applyDdsPeer();
#endif
    initRclcpp(argc, argv);
    const char* path = std::getenv("VIPEE_VHAL_SOCKET");
    PlatformLogger logger;
    PlatformCameraLogger camera_logger;
    auto node = std::make_shared<vipee::BridgeNode>(
        rclcpp::NodeOptions(), path ? path : "/dev/socket/vipee_vhal", logger,
        camera_logger);
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.spin();
    rclcpp::shutdown();
  } catch (const std::exception& e) {
    logError((std::string("fatal: ") + e.what()).c_str());
    return 1;
  }
  return 0;
}
