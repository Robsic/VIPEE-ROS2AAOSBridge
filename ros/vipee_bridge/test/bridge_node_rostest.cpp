#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "test_server.h"
#include "vipee_bridge/bridge_node.h"

namespace vipee {
namespace {

using sirv_msgs::msg::CAT793FVehicleControl;
using sirv_msgs::msg::SirvVehicleControl;
using sirv_msgs::msg::SirvVehicleTelemetry;

class RecordingLogger : public Logger {
 public:
  void transition(int32_t prop, Status status, Reason reason) override {
    entries.push_back({prop, status, reason});
  }
  struct Entry {
    int32_t prop;
    Status status;
    Reason reason;
  };
  std::vector<Entry> entries;
};

std::string MapPath(const std::string& vehicle) {
  return std::string(VIPEE_MAPS_DIR) + "/" + vehicle + ".yaml";
}

// Text of maps/cat793f.yaml without the stale_ms line (a missing field).
std::string Cat793fMapWithoutStaleMs() {
  std::ifstream in(MapPath("cat793f"));
  std::stringstream out;
  std::string l;
  while (std::getline(in, l)) {
    if (l.find("stale_ms") == std::string::npos) out << l << "\n";
  }
  return out.str();
}

class BridgeNodeTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }

  void SetUp() override { server_.Start(); }

  // Starts the bridge node on the given params file.
  void StartNode(const std::string& params_file) {
    rclcpp::NodeOptions opts;
    opts.arguments({"--ros-args", "--params-file", params_file});
    node_ = std::make_shared<BridgeNode>(opts, server_.path(), logger_);
    test_node_ = std::make_shared<rclcpp::Node>("vipee_test_pub");
    exec_.add_node(node_);
    exec_.add_node(test_node_);
  }

  // Spins both nodes and collects socket records until done() or 5 s.
  bool SpinUntil(const std::function<bool()>& done) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < end) {
      exec_.spin_some(std::chrono::milliseconds(20));
      for (const auto& r : server_.Drain()) records_.push_back(r);
      if (done()) return true;
    }
    return false;
  }

  template <typename Msg>
  typename rclcpp::Publisher<Msg>::SharedPtr Publisher(const std::string& topic,
                                                       size_t depth) {
    auto pub = test_node_->create_publisher<Msg>(topic, rclcpp::QoS(depth));
    EXPECT_TRUE(SpinUntil([&] { return pub->get_subscription_count() > 0; }))
        << "no subscriber on " << topic;
    return pub;
  }

  std::optional<WireRecord> Last(int32_t prop) const {
    std::optional<WireRecord> last;
    for (const auto& r : records_) {
      if (r.kind == static_cast<uint16_t>(WireKind::kUpdate) &&
          r.prop_id == prop) {
        last = r;
      }
    }
    return last;
  }

  bool Has(int32_t prop, Status status, int32_t int_value) const {
    const auto r = Last(prop);
    return r && r->status == static_cast<int32_t>(status) &&
           r->int_value == int_value;
  }

  TestServer server_;
  RecordingLogger logger_;
  std::shared_ptr<BridgeNode> node_;
  std::shared_ptr<rclcpp::Node> test_node_;
  rclcpp::executors::SingleThreadedExecutor exec_;
  std::vector<WireRecord> records_;
};

TEST_F(BridgeNodeTest, Cat793fMapDeliversTelemetryLeverAndHandbrake) {
  StartNode(MapPath("cat793f"));
  ASSERT_EQ(MapError::kNone, node_->mapError());
  auto telemetry = Publisher<SirvVehicleTelemetry>("/cat793f/telemetry", 5);
  auto lever = Publisher<CAT793FVehicleControl>("/cat793f/vehicle_control", 1);
  auto control = Publisher<SirvVehicleControl>("/apply_control_to_vehicle", 2);

  SirvVehicleTelemetry t;
  t.speed = 36.0f;
  t.gear = 0;  // neutral engaged, lever says P
  telemetry->publish(t);
  CAT793FVehicleControl l;
  l.transmission = 80;  // 'P'
  lever->publish(l);
  SirvVehicleControl c;
  c.id = 3;
  c.handbrake = true;
  control->publish(c);

  ASSERT_TRUE(SpinUntil([&] {
    return Last(prop::kPerfVehicleSpeed) &&
           Has(prop::kGearSelection, Status::kAvailable, gear::kPark) &&
           Has(prop::kParkingBrakeOn, Status::kAvailable, 1);
  }));
  EXPECT_EQ(10.0f, Last(prop::kPerfVehicleSpeed)->float_value);
  EXPECT_EQ(0, Last(prop::kPerfVehicleSpeed)->status);
  EXPECT_TRUE(Has(prop::kCurrentGear, Status::kAvailable, gear::kNeutral));
  EXPECT_TRUE(Has(prop::kGearSelection, Status::kAvailable, gear::kPark));
  EXPECT_GT(Last(prop::kPerfVehicleSpeed)->timestamp_ns, 0);
}

TEST_F(BridgeNodeTest, CommanderMapDerivesSelectionAndIgnoresTheLever) {
  StartNode(MapPath("commander"));
  ASSERT_EQ(MapError::kNone, node_->mapError());
  auto telemetry = Publisher<SirvVehicleTelemetry>("/commander/telemetry", 5);

  SirvVehicleTelemetry t;
  t.speed = -18.0f;
  t.gear = 0;
  telemetry->publish(t);

  ASSERT_TRUE(SpinUntil([&] {
    return Has(prop::kGearSelection, Status::kAvailable, gear::kNeutral);
  }));
  EXPECT_EQ(-5.0f, Last(prop::kPerfVehicleSpeed)->float_value);
  EXPECT_TRUE(Has(prop::kCurrentGear, Status::kAvailable, gear::kNeutral));
}

TEST_F(BridgeNodeTest, SourceSilentForMoreThanStaleMsBecomesUnavailable) {
  StartNode(MapPath("commander"));
  auto telemetry = Publisher<SirvVehicleTelemetry>("/commander/telemetry", 5);
  SirvVehicleTelemetry t;
  t.speed = 36.0f;
  t.gear = 1;
  telemetry->publish(t);
  ASSERT_TRUE(SpinUntil([&] {
    return Last(prop::kPerfVehicleSpeed) &&
           Last(prop::kPerfVehicleSpeed)->status == 0;
  }));

  ASSERT_TRUE(SpinUntil([&] {
    return Last(prop::kPerfVehicleSpeed)->status == 1 &&
           Last(prop::kCurrentGear)->status == 1;
  }));
  bool stale_logged = false;
  for (const auto& e : logger_.entries) {
    stale_logged |= e.prop == prop::kPerfVehicleSpeed &&
                    e.status == Status::kUnavailable &&
                    e.reason == Reason::kStale;
  }
  EXPECT_TRUE(stale_logged);
}

TEST_F(BridgeNodeTest, MapWithMissingParameterLeavesFourUnavailableAndAlive) {
  char path[] = "/tmp/vipee_map_XXXXXX";
  const int fd = mkstemp(path);
  ASSERT_GE(fd, 0);
  close(fd);
  std::ofstream(path) << Cat793fMapWithoutStaleMs();
  StartNode(path);
  EXPECT_EQ(MapError::kBadStaleMs, node_->mapError());

  // Alive: the sink still connects and sends heartbeats after the four
  // MAP_INVALID records.
  ASSERT_TRUE(SpinUntil([&] {
    size_t hb = 0;
    for (const auto& r : records_) {
      hb += r.kind == static_cast<uint16_t>(WireKind::kHeartbeat);
    }
    return hb >= 2 && Last(prop::kParkingBrakeOn);
  }));
  for (const int32_t p : {prop::kPerfVehicleSpeed, prop::kGearSelection,
                          prop::kCurrentGear, prop::kParkingBrakeOn}) {
    ASSERT_TRUE(Last(p).has_value()) << p;
    EXPECT_EQ(1, Last(p)->status) << p;
  }
  size_t invalid = 0;
  for (const auto& e : logger_.entries) {
    invalid +=
        e.status == Status::kUnavailable && e.reason == Reason::kMapInvalid;
  }
  EXPECT_EQ(4u, invalid);
  unlink(path);
}

}  // namespace
}  // namespace vipee
