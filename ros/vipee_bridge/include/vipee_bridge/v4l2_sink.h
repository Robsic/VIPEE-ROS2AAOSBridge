/**
 * @file v4l2_sink.h
 * @brief FrameSink that writes YUYV frames to a V4L2 output device
 * (v4l2loopback in the product). Linux only: no ROS, no Android.
 */
#ifndef VIPEE_BRIDGE_V4L2_SINK_H_
#define VIPEE_BRIDGE_V4L2_SINK_H_

#include <string>

#include "vipee/frame_sink.h"
#include "vipee/vehicle_map.h"

namespace vipee {

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
