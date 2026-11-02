/**
 * @file frame_sink.h
 * @brief Receiver of YUYV camera frames: the v4l2loopback device in the
 * product, a fake in the tests.
 */
#ifndef VIPEE_FRAME_SINK_H_
#define VIPEE_FRAME_SINK_H_

#include <cstddef>
#include <cstdint>

namespace vipee {

/// @brief One video output device of one camera.
class FrameSink {
 public:
  virtual ~FrameSink() = default;

  /**
   * @brief Opens the device and fixes its format (YUYV, map width × height).
   * @return true when the device is ready for write().
   */
  virtual bool open() = 0;

  /**
   * @brief Writes one whole frame. A failure closes the device.
   * @param yuyv YUYV frame, width × height × 2 bytes (see jpeg_yuyv.h).
   * @param len Size of yuyv in bytes.
   * @return true when the frame was written.
   */
  virtual bool write(const uint8_t* yuyv, size_t len) = 0;

  /// @brief true between a successful open() and a failed write().
  virtual bool isOpen() const = 0;
};

}  // namespace vipee

#endif  // VIPEE_FRAME_SINK_H_
