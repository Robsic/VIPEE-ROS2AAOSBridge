#include "vipee_bridge/v4l2_sink.h"

#include <dirent.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace vipee {
namespace {

CameraConfig Config(const std::string& device) {
  CameraConfig c;
  c.name = "front";
  c.device = device;
  c.width = 1280;
  c.height = 720;
  return c;
}

int OpenFdCount() {
  int n = 0;
  DIR* d = opendir("/proc/self/fd");
  while (readdir(d) != nullptr) ++n;
  closedir(d);
  return n;
}

TEST(V4l2SinkTest, MissingDeviceFailsToOpen) {
  V4l2Sink sink(Config("/nonexistent/vipee_video"));
  EXPECT_FALSE(sink.open());
  EXPECT_FALSE(sink.isOpen());
}

TEST(V4l2SinkTest, RegularFileFailsSFmtAndClosesTheDescriptor) {
  char path[] = "/tmp/vipee_v4l2_XXXXXX";
  const int fd = mkstemp(path);
  ASSERT_GE(fd, 0);
  close(fd);
  const int before = OpenFdCount();
  V4l2Sink sink(Config(path));
  EXPECT_FALSE(sink.open());
  EXPECT_FALSE(sink.isOpen());
  EXPECT_EQ(before, OpenFdCount());
  unlink(path);
}

TEST(V4l2SinkTest, WriteWithoutOpenFails) {
  V4l2Sink sink(Config("/nonexistent/vipee_video"));
  const std::vector<uint8_t> frame(size_t{1280} * 720 * 2);
  EXPECT_FALSE(sink.write(frame.data(), frame.size()));
  EXPECT_FALSE(sink.isOpen());
}

// What VIDIOC_S_FMT returns when the driver takes the 1280x720 request as is.
v4l2_pix_format Yuyv720() {
  v4l2_pix_format f{};
  f.pixelformat = V4L2_PIX_FMT_YUYV;
  f.width = 1280;
  f.height = 720;
  return f;
}

TEST(V4l2SinkTest, ExactYuyvFormatIsAccepted) {
  EXPECT_TRUE(v4l2FormatAccepted(Yuyv720(), 1280, 720));
}

TEST(V4l2SinkTest, DriverAdjustedFormatIsRefused) {
  v4l2_pix_format taller = Yuyv720();
  taller.height = 736;
  EXPECT_FALSE(v4l2FormatAccepted(taller, 1280, 720));
  v4l2_pix_format wider = Yuyv720();
  wider.width = 1288;
  EXPECT_FALSE(v4l2FormatAccepted(wider, 1280, 720));
  v4l2_pix_format uyvy = Yuyv720();
  uyvy.pixelformat = V4L2_PIX_FMT_UYVY;
  EXPECT_FALSE(v4l2FormatAccepted(uyvy, 1280, 720));
}

TEST(V4l2SinkTest, OnlyAFullWriteIsComplete) {
  const size_t len = size_t{1280} * 720 * 2;
  EXPECT_TRUE(v4l2WriteComplete(static_cast<ssize_t>(len), len));
  EXPECT_FALSE(v4l2WriteComplete(static_cast<ssize_t>(len) - 1, len));
  EXPECT_FALSE(v4l2WriteComplete(0, len));
  EXPECT_FALSE(v4l2WriteComplete(-1, len));
}

}  // namespace
}  // namespace vipee
