#include "vipee_bridge/v4l2_sink.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>

namespace vipee {

V4l2Sink::V4l2Sink(const CameraConfig& config)
    : device_(config.device), width_(config.width), height_(config.height) {}

V4l2Sink::~V4l2Sink() { close(); }

void V4l2Sink::close() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

bool V4l2Sink::open() {
  if (fd_ >= 0) return true;
  const int fd = ::open(device_.c_str(), O_RDWR);
  if (fd < 0) return false;
  v4l2_format f{};
  f.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  f.fmt.pix.width = static_cast<__u32>(width_);
  f.fmt.pix.height = static_cast<__u32>(height_);
  f.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
  f.fmt.pix.field = V4L2_FIELD_NONE;
  f.fmt.pix.bytesperline = static_cast<__u32>(width_) * 2;
  f.fmt.pix.sizeimage =
      static_cast<__u32>(width_) * static_cast<__u32>(height_) * 2;
  // The driver may adjust the request; a different format would make the
  // reader see garbage, so anything but the exact request is a failure.
  if (ioctl(fd, VIDIOC_S_FMT, &f) < 0 ||
      f.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV ||
      f.fmt.pix.width != static_cast<__u32>(width_) ||
      f.fmt.pix.height != static_cast<__u32>(height_)) {
    ::close(fd);
    return false;
  }
  fd_ = fd;
  return true;
}

bool V4l2Sink::write(const uint8_t* yuyv, size_t len) {
  if (fd_ < 0) return false;
  ssize_t n;
  do {
    n = ::write(fd_, yuyv, len);
  } while (n < 0 && errno == EINTR);
  if (n != static_cast<ssize_t>(len)) {
    close();
    return false;
  }
  return true;
}

}  // namespace vipee
