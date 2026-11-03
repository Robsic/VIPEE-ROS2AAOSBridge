/**
 * @file v4l2_sink.h
 * @brief FrameSink that writes YUYV frames to a V4L2 output device
 * (v4l2loopback in the product). Linux only: no ROS, no Android.
 */
#ifndef VIPEE_BRIDGE_V4L2_SINK_H_
#define VIPEE_BRIDGE_V4L2_SINK_H_

#include <linux/videodev2.h>
#include <sys/types.h>

#include <cstddef>
#include <string>

#include "vipee/frame_sink.h"
#include "vipee/vehicle_map.h"

namespace vipee {

/**
 * @brief Checks the format VIDIOC_S_FMT returned against the request.
 *
 * The driver may adjust the request; any change would make the reader see
 * garbage.
 * @param got Pixel format filled in by VIDIOC_S_FMT.
 * @param width Requested width, px.
 * @param height Requested height, px.
 * @return true only for V4L2_PIX_FMT_YUYV with exactly width × height.
 */
bool v4l2FormatAccepted(const v4l2_pix_format& got, int width, int height);

/**
 * @brief Checks that one write() call wrote the whole frame.
 * @param written Return value of write(), -1 on error.
 * @param len Frame size in bytes.
 * @return true only when written == len; a short or failed write is false.
 */
bool v4l2WriteComplete(ssize_t written, size_t len);

/**
 * @brief One v4l2loopback output device.
 *
 * open() fixes the format (YUYV, width × height, bytesperline = 2 × width)
 * before any reader opens the camera. The descriptor stays open while the
 * process lives, even without frames. Not thread-safe.
 */
class V4l2Sink : public FrameSink {
 public:
  /**
   * @param config Validated camera; device, width and height are used.
   */
  explicit V4l2Sink(const CameraConfig& config);
  ~V4l2Sink() override;
  V4l2Sink(const V4l2Sink&) = delete;
  V4l2Sink& operator=(const V4l2Sink&) = delete;

  /**
   * @brief Opens the device O_RDWR and sets the V4L2 output format.
   * @return true when ready; false (descriptor closed) when the open or
   * VIDIOC_S_FMT failed, or when the device did not accept the exact size.
   */
  bool open() override;

  /**
   * @brief One write() of the whole frame. A short or failed write closes the
   * device.
   * @param yuyv YUYV frame, width × height × 2 bytes.
   * @param len Size of yuyv in bytes.
   * @return true when all len bytes were written.
   */
  bool write(const uint8_t* yuyv, size_t len) override;

  /// @brief true between a successful open() and a failed write().
  bool isOpen() const override { return fd_ >= 0; }

 private:
  void close();

  std::string device_;
  int width_;
  int height_;
  int fd_ = -1;
};

}  // namespace vipee

#endif  // VIPEE_BRIDGE_V4L2_SINK_H_
