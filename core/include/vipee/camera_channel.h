/**
 * @file camera_channel.h
 * @brief State of one camera: validate, decode and write each frame once,
 * staleness, drop reasons and device recovery (spec CAM-01, 03, 04, 06, 07,
 * 12..14, 22).
 *
 * No ROS and no Android. Not thread-safe: one thread calls onFrame(), tick()
 * and takeStats(). Times are CLOCK_BOOTTIME ns.
 */
#ifndef VIPEE_CAMERA_CHANNEL_H_
#define VIPEE_CAMERA_CHANNEL_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "vipee/frame_sink.h"
#include "vipee/jpeg_yuyv.h"
#include "vipee/vehicle_map.h"

namespace vipee {

/// @brief Receiver of camera log lines (logcat tag vipee_bridge).
class CameraLogger {
 public:
  virtual ~CameraLogger() = default;
  /**
   * @brief One line per transition, e.g. "CAM front STALE".
   * @param line Text without trailing newline.
   */
  virtual void log(const std::string& line) = 0;
};

/// @brief Counters since the previous takeStats().
struct CameraStats {
  int64_t frames = 0;               ///< Frames written to the device.
  int64_t format_unsupported = 0;   ///< Drops FORMAT_UNSUPPORTED.
  int64_t decode_error = 0;         ///< Drops DECODE_ERROR.
  int64_t frame_size_mismatch = 0;  ///< Drops FRAME_SIZE_MISMATCH.
  /// 95th percentile (nearest rank) of rx ➔ written latency in ns; 0 when no
  /// frame was written.
  int64_t p95_latency_ns = 0;
};

/**
 * @brief One camera from its CameraConfig to its FrameSink.
 *
 * Data state: NO_DATA (start) ➔ LIVE on the first valid frame ➔ STALE after
 * more than stale_ms without a valid frame ➔ LIVE on the next valid frame.
 * Only a valid frame renews the deadline; a dropped frame counts as absent.
 * Device state, independent: the device opens on the first onFrame() or
 * tick(); after a failed open() or write() it is retried at most once per
 * second. Each transition is logged once: "CAM <name> LIVE", "... STALE",
 * "... DROP <REASON>" (when the drop reason changes), "...
 * DEVICE_UNAVAILABLE", "... DEVICE_OK".
 */
class CameraChannel {
 public:
  /**
   * @param config Validated camera (copied).
   * @param sink Device of this camera; must outlive the channel.
   * @param logger Log receiver; must outlive the channel.
   */
  CameraChannel(const CameraConfig& config, FrameSink& sink,
                CameraLogger& logger);

  /**
   * @brief Handles one CompressedImage: decodes it and, if valid, writes it
   * to the device exactly once. Frames are never rewritten later.
   * @param format CompressedImage.format.
   * @param data JPEG bytes.
   * @param len Size of data.
   * @param rx_ns Arrival of the message in the ROS process; only used for the
   * latency (header.stamp plays no role, so a bag loop changes nothing).
   * @param now_ns Start of the handling.
   */
  void onFrame(std::string_view format, const uint8_t* data, size_t len,
               int64_t rx_ns, int64_t now_ns);

  /**
   * @brief Applies the staleness limit and retries a missing device. Never
   * writes a frame.
   * @param now_ns Current time.
   */
  void tick(int64_t now_ns);

  /// @brief Returns and resets the counters.
  CameraStats takeStats();

 private:
  enum class Data { kNoData, kLive, kStale };

  void ensureOpen(int64_t now_ns);
  void deviceLost(int64_t now_ns);
  void log(const char* what);

  CameraConfig config_;
  FrameSink& sink_;
  CameraLogger& logger_;
  Data data_ = Data::kNoData;
  int64_t last_valid_ns_ = 0;
  DropReason last_drop_ = DropReason::kNone;
  bool device_unavailable_ = false;
  bool tried_open_ = false;
  int64_t last_open_try_ns_ = 0;
  std::vector<uint8_t> yuyv_;
  CameraStats stats_;
  std::vector<int64_t> latencies_ns_;
};

}  // namespace vipee

#endif  // VIPEE_CAMERA_CHANNEL_H_
