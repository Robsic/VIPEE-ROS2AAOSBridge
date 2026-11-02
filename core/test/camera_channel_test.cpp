#include "vipee/camera_channel.h"

#include <gtest/gtest.h>
#include <turbojpeg.h>

#include <cstdint>
#include <string>
#include <vector>

namespace vipee {
namespace {

constexpr int64_t kMs = 1000000;
constexpr int64_t kT0 = 1000 * kMs;
constexpr int kW = 64;
constexpr int kH = 32;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<uint8_t> makeJpeg(int w, int h) {
  std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
  for (size_t i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<uint8_t>(i * 7);
  tjhandle tj = tj3Init(TJINIT_COMPRESS);
  tj3Set(tj, TJPARAM_SUBSAMP, TJSAMP_420);
  tj3Set(tj, TJPARAM_QUALITY, 90);
  unsigned char* buf = nullptr;
  size_t size = 0;
  EXPECT_EQ(tj3Compress8(tj, rgb.data(), w, 0, h, TJPF_RGB, &buf, &size), 0);
  std::vector<uint8_t> jpeg(buf, buf + size);
  tj3Free(buf);
  tj3Destroy(tj);
  return jpeg;
}

class FakeSink : public FrameSink {
 public:
  bool open() override {
    ++opens;
    open_ = open_ok;
    return open_;
  }
  bool write(const uint8_t* yuyv, size_t len) override {
    if (!open_ || !write_ok) {
      open_ = false;
      return false;
    }
    frames.emplace_back(yuyv, yuyv + len);
    return true;
  }
  bool isOpen() const override { return open_; }

  bool open_ok = true;
  bool write_ok = true;
  int opens = 0;
  std::vector<std::vector<uint8_t>> frames;

 private:
  bool open_ = false;
};

class FakeLogger : public CameraLogger {
 public:
  void log(const std::string& line) override { lines.push_back(line); }
  std::vector<std::string> lines;
};

class CameraChannelTest : public ::testing::Test {
 protected:
  CameraChannelTest() : channel_(Config(), sink_, log_) {}

  static CameraConfig Config() {
    return {"front",
            EvsView::kFront,
            "/sirv/sensor/fisheye/front/image/compressed",
            "/dev/video10",
            kW,
            kH,
            500};
  }

  void Frame(int64_t now_ns) { Frame(good_, "jpeg", now_ns); }
  void Frame(const std::vector<uint8_t>& data, const char* format,
             int64_t now_ns) {
    channel_.onFrame(format, data.data(), data.size(), now_ns, now_ns);
  }

  std::vector<uint8_t> good_ = makeJpeg(kW, kH);
  FakeSink sink_;
  FakeLogger log_;
  CameraChannel channel_;
};

using Lines = std::vector<std::string>;

TEST_F(CameraChannelTest, ValidFrameIsWrittenOnceAsYuyvOfTheMapSize) {
  std::vector<uint8_t> expected;
  ASSERT_EQ(
      decodeJpegToYuyv("jpeg", good_.data(), good_.size(), kW, kH, &expected),
      DropReason::kNone);

  Frame(kT0);
  ASSERT_EQ(sink_.frames.size(), 1u);
  EXPECT_EQ(sink_.frames[0].size(), static_cast<size_t>(kW) * kH * 2);
  EXPECT_EQ(sink_.frames[0], expected);
  EXPECT_EQ(log_.lines, (Lines{"CAM front LIVE"}));
}

TEST_F(CameraChannelTest, TickWithoutNewFrameWritesNothing) {
  Frame(kT0);
  for (int64_t t = 50; t <= 2000; t += 50) channel_.tick(kT0 + t * kMs);
  EXPECT_EQ(sink_.frames.size(), 1u);
}

TEST_F(CameraChannelTest, StaleOnlyAfterMoreThanStaleMs) {
  Frame(kT0);
  channel_.tick(kT0 + 500 * kMs);
  EXPECT_EQ(log_.lines, (Lines{"CAM front LIVE"}));
  channel_.tick(kT0 + 500 * kMs + 1);
  channel_.tick(kT0 + 900 * kMs);
  EXPECT_EQ(log_.lines, (Lines{"CAM front LIVE", "CAM front STALE"}));
}

TEST_F(CameraChannelTest, NextValidFrameAfterStaleIsLiveAndWritten) {
  Frame(kT0);
  channel_.tick(kT0 + 600 * kMs);
  Frame(kT0 + 700 * kMs);
  EXPECT_EQ(sink_.frames.size(), 2u);
  EXPECT_EQ(log_.lines,
            (Lines{"CAM front LIVE", "CAM front STALE", "CAM front LIVE"}));
}

TEST_F(CameraChannelTest, NoStaleBeforeTheFirstFrame) {
  channel_.tick(kT0);
  channel_.tick(kT0 + 5000 * kMs);
  EXPECT_TRUE(log_.lines.empty());
  EXPECT_TRUE(sink_.frames.empty());
}

TEST_F(CameraChannelTest, DroppedFrameDoesNotRenewTheDeadline) {
  Frame(kT0);
  Frame(good_, "png", kT0 + 400 * kMs);
  channel_.tick(kT0 + 500 * kMs + 1);
  EXPECT_EQ(sink_.frames.size(), 1u);
  EXPECT_EQ(log_.lines.back(), "CAM front STALE");
}

TEST_F(CameraChannelTest, DropIsLoggedOncePerReasonChange) {
  std::vector<uint8_t> truncated(
      good_.begin(), good_.begin() + static_cast<ptrdiff_t>(good_.size() / 2));
  const std::vector<uint8_t> small = makeJpeg(32, 32);
  Frame(good_, "png", kT0);
  Frame(good_, "png", kT0 + 10 * kMs);
  Frame(truncated, "jpeg", kT0 + 20 * kMs);
  Frame(truncated, "jpeg", kT0 + 30 * kMs);
  Frame(small, "jpeg", kT0 + 40 * kMs);
  Frame(small, "jpeg", kT0 + 50 * kMs);
  EXPECT_TRUE(sink_.frames.empty());
  EXPECT_EQ(log_.lines, (Lines{"CAM front DROP FORMAT_UNSUPPORTED",
                               "CAM front DROP DECODE_ERROR",
                               "CAM front DROP FRAME_SIZE_MISMATCH"}));
  // A valid frame ends the drop, so the same reason afterwards is a change.
  Frame(kT0 + 60 * kMs);
  Frame(small, "jpeg", kT0 + 70 * kMs);
  EXPECT_EQ(log_.lines.back(), "CAM front DROP FRAME_SIZE_MISMATCH");
  EXPECT_EQ(log_.lines.size(), 5u);
}

TEST_F(CameraChannelTest,
       WriteFailureMarksDeviceUnavailableAndRetriesEachSecond) {
  Frame(kT0);
  ASSERT_EQ(sink_.opens, 1);
  sink_.write_ok = false;
  sink_.open_ok = false;
  Frame(kT0 + 100 * kMs);  // write fails at t = 100 ms
  EXPECT_EQ(log_.lines.back(), "CAM front DEVICE_UNAVAILABLE");

  Frame(kT0 + 200 * kMs);
  channel_.tick(kT0 + 1099 * kMs);
  EXPECT_EQ(sink_.opens, 1);  // no retry before 1 s
  channel_.tick(kT0 + 1100 * kMs);
  EXPECT_EQ(sink_.opens, 2);  // retry, fails again
  channel_.tick(kT0 + 1500 * kMs);
  EXPECT_EQ(sink_.opens, 2);

  sink_.open_ok = true;
  sink_.write_ok = true;
  channel_.tick(kT0 + 2100 * kMs);
  EXPECT_EQ(sink_.opens, 3);
  EXPECT_EQ(log_.lines.back(), "CAM front DEVICE_OK");
  Frame(kT0 + 2150 * kMs);
  EXPECT_EQ(sink_.frames.size(), 2u);  // first and last frame only

  int unavailable = 0;
  for (const auto& l : log_.lines) {
    unavailable += l == "CAM front DEVICE_UNAVAILABLE";
  }
  EXPECT_EQ(unavailable, 1);
}

TEST_F(CameraChannelTest, DeviceThatDoesNotOpenIsUnavailableAndRetried) {
  sink_.open_ok = false;
  channel_.tick(kT0);
  EXPECT_EQ(log_.lines, (Lines{"CAM front DEVICE_UNAVAILABLE"}));
  Frame(kT0 + 500 * kMs);
  EXPECT_EQ(sink_.opens, 1);
  EXPECT_TRUE(sink_.frames.empty());
  sink_.open_ok = true;
  Frame(kT0 + 1000 * kMs);
  EXPECT_EQ(sink_.opens, 2);
  EXPECT_EQ(sink_.frames.size(), 1u);
}

TEST_F(CameraChannelTest, ArrivalOrderAloneDecidesDelivery) {
  // A bag loop moves header.stamp back; the channel only sees arrivals and
  // writes each frame, even one whose rx is before the previous one.
  channel_.onFrame("jpeg", good_.data(), good_.size(), kT0 + 300 * kMs,
                   kT0 + 310 * kMs);
  channel_.onFrame("jpeg", good_.data(), good_.size(), kT0 + 100 * kMs,
                   kT0 + 320 * kMs);
  EXPECT_EQ(sink_.frames.size(), 2u);
  EXPECT_EQ(log_.lines, (Lines{"CAM front LIVE"}));
}

TEST_F(CameraChannelTest, StatsCountFramesDropsAndP95Latency) {
  const std::vector<uint8_t> small = makeJpeg(32, 32);
  // 20 frames, rx ➔ handling latency 100, 200, ..., 2000 ms.
  for (int64_t k = 1; k <= 20; ++k) {
    const int64_t now = kT0 + k * 10 * kMs;
    channel_.onFrame("jpeg", good_.data(), good_.size(), now - k * 100 * kMs,
                     now);
  }
  Frame(good_, "png", kT0 + 300 * kMs);
  Frame(good_, "", kT0 + 310 * kMs);
  Frame(std::vector<uint8_t>(16, 0x42), "jpeg", kT0 + 320 * kMs);
  Frame(small, "jpeg", kT0 + 330 * kMs);
  Frame(small, "jpeg", kT0 + 340 * kMs);
  Frame(small, "jpeg", kT0 + 350 * kMs);

  const CameraStats s = channel_.takeStats();
  EXPECT_EQ(s.frames, 20);
  EXPECT_EQ(s.format_unsupported, 2);
  EXPECT_EQ(s.decode_error, 1);
  EXPECT_EQ(s.frame_size_mismatch, 3);
  // Nearest rank: the 19th of 20 is 1900 ms, plus decode and write time.
  EXPECT_GE(s.p95_latency_ns, 1900 * kMs);
  EXPECT_LT(s.p95_latency_ns, 2000 * kMs);

  const CameraStats empty = channel_.takeStats();
  EXPECT_EQ(empty.frames, 0);
  EXPECT_EQ(
      empty.format_unsupported + empty.decode_error + empty.frame_size_mismatch,
      0);
  EXPECT_EQ(empty.p95_latency_ns, 0);
}

}  // namespace
}  // namespace vipee
