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

}  // namespace
}  // namespace vipee
