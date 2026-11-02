#include "vipee/camera_channel.h"

#include <algorithm>
#include <chrono>

namespace vipee {
namespace {

constexpr int64_t kMsNs = 1000000;
constexpr int64_t kReopenNs = 1000 * kMsNs;  // CAM-14: retry every 1 s.

const char* dropName(DropReason r) {
  switch (r) {
    case DropReason::kFormatUnsupported:
      return "DROP FORMAT_UNSUPPORTED";
    case DropReason::kDecodeError:
      return "DROP DECODE_ERROR";
    case DropReason::kFrameSizeMismatch:
      return "DROP FRAME_SIZE_MISMATCH";
    case DropReason::kNone:
      break;
  }
  return "DROP NONE";
}

int64_t steadyNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

CameraChannel::CameraChannel(const CameraConfig& config, FrameSink& sink,
                             CameraLogger& logger)
    : config_(config), sink_(sink), logger_(logger) {}

void CameraChannel::onFrame(
    std::string_view format, const uint8_t* data,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    size_t len, int64_t rx_ns, int64_t now_ns) {
  const int64_t start = steadyNs();
  const DropReason r = decodeJpegToYuyv(format, data, len, config_.width,
                                        config_.height, &yuyv_);
  if (r != DropReason::kNone) {
    if (r == DropReason::kFormatUnsupported) ++stats_.format_unsupported;
    if (r == DropReason::kDecodeError) ++stats_.decode_error;
    if (r == DropReason::kFrameSizeMismatch) ++stats_.frame_size_mismatch;
    if (r != last_drop_) log(dropName(r));
    last_drop_ = r;
    return;
  }
  last_drop_ = DropReason::kNone;
  last_valid_ns_ = now_ns;
  if (data_ != Data::kLive) log("LIVE");
  data_ = Data::kLive;

  ensureOpen(now_ns);
  if (!sink_.isOpen()) return;
  if (!sink_.write(yuyv_.data(), yuyv_.size())) {
    deviceLost(now_ns);
    return;
  }
  ++stats_.frames;
  // SPEC_DEVIATION: design.md says latency = now - rx; this adds the decode
  // and write time measured here.
  // Reason: CAM-03 measures until the frame is in the device, after write().
  latencies_ns_.push_back(now_ns - rx_ns + (steadyNs() - start));
}

void CameraChannel::tick(int64_t now_ns) {
  ensureOpen(now_ns);
  if (data_ == Data::kLive &&
      now_ns - last_valid_ns_ > config_.stale_ms * kMsNs) {
    data_ = Data::kStale;
    log("STALE");
  }
}

CameraStats CameraChannel::takeStats() {
  CameraStats s = stats_;
  if (!latencies_ns_.empty()) {
    // Nearest rank: the ceil(0.95 n)-th smallest.
    const size_t rank = (latencies_ns_.size() * 95 + 99) / 100;
    std::nth_element(latencies_ns_.begin(),
                     latencies_ns_.begin() + static_cast<ptrdiff_t>(rank - 1),
                     latencies_ns_.end());
    s.p95_latency_ns = latencies_ns_[rank - 1];
  }
  stats_ = CameraStats{};
  latencies_ns_.clear();
  return s;
}

void CameraChannel::ensureOpen(int64_t now_ns) {
  if (sink_.isOpen()) return;
  if (tried_open_ && now_ns - last_open_try_ns_ < kReopenNs) return;
  tried_open_ = true;
  last_open_try_ns_ = now_ns;
  if (!sink_.open()) {
    if (!device_unavailable_) log("DEVICE_UNAVAILABLE");
    device_unavailable_ = true;
    return;
  }
  if (device_unavailable_) log("DEVICE_OK");
  device_unavailable_ = false;
}

void CameraChannel::deviceLost(int64_t now_ns) {
  tried_open_ = true;
  last_open_try_ns_ = now_ns;
  if (!device_unavailable_) log("DEVICE_UNAVAILABLE");
  device_unavailable_ = true;
}

void CameraChannel::log(const char* what) {
  logger_.log("CAM " + config_.name + " " + what);
}

}  // namespace vipee
