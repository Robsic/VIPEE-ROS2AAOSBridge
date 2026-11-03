#include <gtest/gtest.h>
#include <turbojpeg.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/compressed_image.hpp"
#include "test_server.h"
#include "vipee_bridge/bridge_node.h"

namespace vipee {
namespace {

using sensor_msgs::msg::CompressedImage;
using sirv_msgs::msg::SirvVehicleTelemetry;
using Clock = std::chrono::steady_clock;

constexpr int kW = 1280;
constexpr int kH = 720;
constexpr size_t kFrameBytes = size_t{kW} * kH * 2;

// Gray JPEG whose luma is `gray` (the first YUYV byte comes out within ±2).
std::vector<uint8_t> GrayJpeg(uint8_t gray) {
  std::vector<uint8_t> rgb(size_t{kW} * kH * 3, gray);
  tjhandle tj = tj3Init(TJINIT_COMPRESS);
  tj3Set(tj, TJPARAM_SUBSAMP, TJSAMP_420);
  tj3Set(tj, TJPARAM_QUALITY, 95);
  unsigned char* buf = nullptr;
  size_t size = 0;
  EXPECT_EQ(0, tj3Compress8(tj, rgb.data(), kW, 0, kH, TJPF_RGB, &buf, &size));
  std::vector<uint8_t> jpeg(buf, buf + size);
  tj3Free(buf);
  tj3Destroy(tj);
  return jpeg;
}

class TestSink : public FrameSink {
 public:
  bool open() override {
    open_ = true;
    return true;
  }
  bool write(const uint8_t* yuyv, size_t len) override {
    ++started;
    if (delay_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    lens_.push_back(len);
    luma_.push_back(yuyv[0]);
    return true;
  }
  bool isOpen() const override { return open_; }

  size_t Count() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lens_.size();
  }
  std::vector<size_t> Lens() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lens_;
  }
  std::vector<uint8_t> Luma() {
    std::lock_guard<std::mutex> lock(mutex_);
    return luma_;
  }

  std::atomic<int> delay_ms{0};
  std::atomic<int> started{0};  // write() calls entered

 private:
  std::atomic<bool> open_{false};
  std::mutex mutex_;
  std::vector<size_t> lens_;
  std::vector<uint8_t> luma_;
};

class SafeCameraLogger : public CameraLogger {
 public:
  void log(const std::string& line) override {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.push_back(line);
  }
  size_t Count(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<size_t>(std::count(lines_.begin(), lines_.end(), line));
  }

 private:
  std::mutex mutex_;
  std::vector<std::string> lines_;
};

class NullLogger : public Logger {
 public:
  void transition(int32_t, Status, Reason) override {}
};

class CameraBridgeTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }

  void SetUp() override {
    server_.Start();
    rclcpp::NodeOptions opts;
    opts.arguments({"--ros-args", "--params-file",
                    std::string(VIPEE_MAPS_DIR) + "/cat793f.yaml"});
    for (const char* name : {"front", "rear", "left", "right"}) {
      sinks_[name] = std::make_shared<TestSink>();
    }
    node_ = std::make_shared<BridgeNode>(
        opts, server_.path(), logger_, camera_logger_,
        [this](const CameraConfig& c) -> std::unique_ptr<FrameSink> {
          return std::make_unique<Forward>(sinks_.at(c.name));
        });
    ASSERT_EQ(CameraMapError::kNone, node_->cameraMapError());
    test_node_ = std::make_shared<rclcpp::Node>("vipee_test_pub");
    exec_.add_node(node_);
    exec_.add_node(test_node_);
  }

  // Hands the BridgeNode a unique_ptr that forwards to a sink the test keeps.
  class Forward : public FrameSink {
   public:
    explicit Forward(std::shared_ptr<TestSink> s) : s_(std::move(s)) {}
    bool open() override { return s_->open(); }
    bool write(const uint8_t* d, size_t n) override { return s_->write(d, n); }
    bool isOpen() const override { return s_->isOpen(); }

   private:
    std::shared_ptr<TestSink> s_;
  };

  bool SpinUntil(const std::function<bool()>& done, int timeout_ms = 5000) {
    const auto end = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (Clock::now() < end) {
      exec_.spin_some(std::chrono::milliseconds(10));
      for (const auto& r : server_.Drain()) records_.push_back(r);
      if (done()) return true;
    }
    return false;
  }

  void SpinFor(int ms) {
    SpinUntil([] { return false; }, ms);
  }

  rclcpp::Publisher<CompressedImage>::SharedPtr CameraPublisher(
      const std::string& pos) {
    auto pub = test_node_->create_publisher<CompressedImage>(
        "/sirv/sensor/fisheye/" + pos + "/image/compressed",
        rclcpp::SensorDataQoS());
    EXPECT_TRUE(SpinUntil([&] { return pub->get_subscription_count() > 0; }));
    return pub;
  }

  static CompressedImage Message(const std::vector<uint8_t>& jpeg) {
    CompressedImage m;
    m.format = "jpeg";
    m.data = jpeg;
    return m;
  }

  bool HasSpeed() const {
    for (const auto& r : records_) {
      if (r.kind == static_cast<uint16_t>(WireKind::kUpdate) &&
          r.prop_id == prop::kPerfVehicleSpeed && r.status == 0) {
        return true;
      }
    }
    return false;
  }

  TestServer server_;
  NullLogger logger_;
  SafeCameraLogger camera_logger_;
  std::map<std::string, std::shared_ptr<TestSink>> sinks_;
  std::shared_ptr<BridgeNode> node_;
  std::shared_ptr<rclcpp::Node> test_node_;
  rclcpp::executors::SingleThreadedExecutor exec_;
  std::vector<WireRecord> records_;
};

TEST_F(CameraBridgeTest, JpegReachesOnlyTheRightCameraSinkExactlyOnce) {
  auto pub = CameraPublisher("rear");
  pub->publish(Message(GrayJpeg(120)));
  ASSERT_TRUE(SpinUntil([&] { return sinks_["rear"]->Count() >= 1; }));
  SpinFor(400);  // no rewrite and no leak to the other cameras
  EXPECT_EQ(std::vector<size_t>{kFrameBytes}, sinks_["rear"]->Lens());
  EXPECT_NEAR(120, sinks_["rear"]->Luma().at(0), 3);
  for (const char* other : {"front", "left", "right"}) {
    EXPECT_EQ(0u, sinks_[other]->Count()) << other;
  }
  EXPECT_EQ(1u, camera_logger_.Count("CAM rear LIVE"));
}

TEST_F(CameraBridgeTest, SilencedTopicGoesStaleWithoutNewFramesThenLive) {
  auto pub = CameraPublisher("front");
  pub->publish(Message(GrayJpeg(100)));
  ASSERT_TRUE(SpinUntil([&] { return sinks_["front"]->Count() == 1; }));
  // Spec CAM-06: STALE after 500 ms, within 600 ms.
  const auto first_write = Clock::now();
  ASSERT_TRUE(SpinUntil(
      [&] { return camera_logger_.Count("CAM front STALE") == 1; }, 600));
  EXPECT_GE(Clock::now() - first_write, std::chrono::milliseconds(450));
  EXPECT_EQ(1u, sinks_["front"]->Count());

  pub->publish(Message(GrayJpeg(100)));
  // LIVE is logged before write(), so wait for both, then check no rewrite.
  ASSERT_TRUE(SpinUntil([&] {
    return camera_logger_.Count("CAM front LIVE") == 2 &&
           sinks_["front"]->Count() == 2;
  }));
  SpinFor(200);
  EXPECT_EQ(2u, sinks_["front"]->Count());
}

TEST_F(CameraBridgeTest, InvalidFramesFasterThanTheTickStillGoStale) {
  constexpr int kStaleMs = 500;  // cameras.*.stale_ms in maps/cat793f.yaml
  auto pub = CameraPublisher("right");
  pub->publish(Message(GrayJpeg(100)));
  ASSERT_TRUE(SpinUntil([&] { return sinks_["right"]->Count() == 1; }));

  // A non-JPEG frame every 20 ms, faster than the 50 ms tick period.
  CompressedImage bad;
  bad.format = "png";
  bad.data = {1, 2, 3, 4};
  const auto start = Clock::now();
  auto next = start;
  auto feed = [&] {
    if (Clock::now() >= next) {
      pub->publish(bad);
      next += std::chrono::milliseconds(20);
    }
  };
  EXPECT_TRUE(SpinUntil(
      [&] {
        feed();
        return camera_logger_.Count("CAM right STALE") == 1;
      },
      kStaleMs + 200));
  const auto end = start + std::chrono::milliseconds(kStaleMs + 300);
  SpinUntil([&] {
    feed();
    return Clock::now() >= end;
  });
  EXPECT_EQ(1u, sinks_["right"]->Count());
}

TEST_F(CameraBridgeTest, BurstWithSlowSinkDeliversTheNewestFrame) {
  sinks_["left"]->delay_ms = 300;
  auto pub = CameraPublisher("left");
  for (const uint8_t gray : {40, 80, 120, 160, 200}) {
    pub->publish(Message(GrayJpeg(gray)));
  }
  ASSERT_TRUE(SpinUntil([&] {
    const auto luma = sinks_["left"]->Luma();
    return !luma.empty() && luma.back() >= 197 && luma.back() <= 203;
  }));
  SpinFor(700);
  const auto luma = sinks_["left"]->Luma();
  EXPECT_LT(luma.size(), 5u);
  EXPECT_NEAR(200, luma.back(), 3);
}

TEST_F(CameraBridgeTest, StuckCameraDoesNotDelayTelemetry) {
  sinks_["front"]->delay_ms = 1500;
  auto camera = CameraPublisher("front");
  auto telemetry = test_node_->create_publisher<SirvVehicleTelemetry>(
      "/cat793f/telemetry", rclcpp::QoS(5));
  ASSERT_TRUE(
      SpinUntil([&] { return telemetry->get_subscription_count() > 0; }));
  const CompressedImage frame = Message(GrayJpeg(100));
  SirvVehicleTelemetry t;
  t.speed = 36.0f;
  t.gear = 0;

  // Back to back: the speed must arrive while the 1.5 s write is running.
  const auto start = Clock::now();
  camera->publish(frame);
  telemetry->publish(t);
  ASSERT_TRUE(SpinUntil(
      [&] { return HasSpeed() && sinks_["front"]->started == 1; }, 1000));
  EXPECT_LT(Clock::now() - start, std::chrono::milliseconds(500));
  EXPECT_EQ(0u, sinks_["front"]->Count());  // still inside write()
}

}  // namespace
}  // namespace vipee
