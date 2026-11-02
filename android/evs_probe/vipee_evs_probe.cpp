/**
 * @file vipee_evs_probe.cpp
 * @brief Headless EVS client for the cam-evs-slice tests (CAM-02, CAM-19).
 *
 * Talks to evsmanagerd (`IEvsEnumerator/default`) the same way CarEvsService
 * does, opens cameras with the default stream configuration and counts the
 * frames each one delivers. Runs as root (`adb root`): evsmanagerd accepts
 * AID_ROOT clients.
 *
 * Usage:
 *   vipee_evs_probe --list
 *   vipee_evs_probe [--close-after-ms <ms>] <seconds> <camera_id>...
 *
 * Output, one line per camera at the end of the run:
 *   PROBE <id> frames=<n> width=<w> height=<h> format=<f> max_gap_ms=<g>
 *         last_frame_ms_ago=<t> stop_ms=<s>
 * `width`, `height` (px) and `format` (AHardwareBuffer format) come from the
 * last frame and are 0 when no frame arrived. `max_gap_ms` is the largest
 * interval between consecutive frames (ms, steady clock); `last_frame_ms_ago`
 * is the time since the last frame when the stream stops (-1 with no frame).
 * `stop_ms` is how long `stopVideoStream` plus the STREAM_STOPPED event took.
 * With `--close-after-ms`, the stream stops after that many ms instead of at
 * the end of `<seconds>`; the process still waits the full `<seconds>`.
 * `--list` prints `CAMERA <id>` per camera.
 *
 * Exit codes: 0 success; 1 usage; 2 evsmanagerd not available; 3 a camera
 * failed to open or start; 4 a stream did not report STREAM_STOPPED within
 * 2 s of the stop request.
 */
#include <aidl/android/hardware/automotive/evs/BnEvsCameraStream.h>
#include <aidl/android/hardware/automotive/evs/IEvsEnumerator.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using aidl::android::hardware::automotive::evs::BnEvsCameraStream;
using aidl::android::hardware::automotive::evs::BufferDesc;
using aidl::android::hardware::automotive::evs::CameraDesc;
using aidl::android::hardware::automotive::evs::EvsEventDesc;
using aidl::android::hardware::automotive::evs::EvsEventType;
using aidl::android::hardware::automotive::evs::IEvsCamera;
using aidl::android::hardware::automotive::evs::IEvsEnumerator;
using aidl::android::hardware::automotive::evs::Stream;
using Clock = std::chrono::steady_clock;

int64_t msSince(Clock::time_point t) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t)
      .count();
}

// Counts frames and hands each buffer straight back to the camera.
class Counter : public BnEvsCameraStream {
 public:
  explicit Counter(std::shared_ptr<IEvsCamera> camera)
      : camera_(std::move(camera)) {}

  ndk::ScopedAStatus deliverFrame(
      const std::vector<BufferDesc>& buffers) override {
    std::vector<BufferDesc> done;
    {
      std::lock_guard<std::mutex> lock(mu_);
      for (const auto& b : buffers) {
        const auto now = Clock::now();
        if (frames_ > 0) {
          max_gap_ms_ = std::max<int64_t>(
              max_gap_ms_,
              std::chrono::duration_cast<std::chrono::milliseconds>(now - last_)
                  .count());
        }
        last_ = now;
        ++frames_;
        width_ = b.buffer.description.width;
        height_ = b.buffer.description.height;
        format_ = static_cast<int>(b.buffer.description.format);
        BufferDesc ret;
        ret.bufferId = b.bufferId;
        ret.deviceId = b.deviceId;
        done.push_back(std::move(ret));
      }
    }
    camera_->doneWithFrame(done);
    return ndk::ScopedAStatus::ok();
  }

  ndk::ScopedAStatus notify(const EvsEventDesc& event) override {
    if (event.aType == EvsEventType::STREAM_STOPPED) {
      std::lock_guard<std::mutex> lock(mu_);
      stopped_ = true;
      cv_.notify_all();
    }
    return ndk::ScopedAStatus::ok();
  }

  bool waitStopped(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mu_);
    return cv_.wait_for(lock, timeout, [this] { return stopped_; });
  }

  void print(const std::string& id, int64_t stop_ms) {
    std::lock_guard<std::mutex> lock(mu_);
    std::printf(
        "PROBE %s frames=%lld width=%d height=%d format=%d max_gap_ms=%lld "
        "last_frame_ms_ago=%lld stop_ms=%lld\n",
        id.c_str(), static_cast<long long>(frames_), width_, height_, format_,
        static_cast<long long>(max_gap_ms_),
        static_cast<long long>(frames_ > 0 ? msSince(last_) : -1),
        static_cast<long long>(stop_ms));
  }

 private:
  std::shared_ptr<IEvsCamera> camera_;
  std::mutex mu_;
  std::condition_variable cv_;
  int64_t frames_ = 0;
  int64_t max_gap_ms_ = 0;
  Clock::time_point last_;
  int width_ = 0;
  int height_ = 0;
  int format_ = 0;
  bool stopped_ = false;
};

struct Session {
  std::string id;
  std::shared_ptr<IEvsCamera> camera;
  std::shared_ptr<Counter> counter;
  int64_t stop_ms = -1;
  bool stopped = false;
};

int usage() {
  std::fprintf(stderr,
               "usage: vipee_evs_probe --list\n"
               "       vipee_evs_probe [--close-after-ms <ms>] <seconds> "
               "<camera_id>...\n");
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty()) return usage();

  ABinderProcess_setThreadPoolMaxThreadCount(4);
  ABinderProcess_startThreadPool();
  const std::string name = std::string(IEvsEnumerator::descriptor) + "/default";
  if (!AServiceManager_isDeclared(name.c_str())) {
    std::fprintf(stderr, "%s not declared\n", name.c_str());
    return 2;
  }
  auto evs = IEvsEnumerator::fromBinder(
      ndk::SpAIBinder(AServiceManager_waitForService(name.c_str())));
  if (!evs) {
    std::fprintf(stderr, "%s not available\n", name.c_str());
    return 2;
  }

  if (args[0] == "--list") {
    std::vector<CameraDesc> cams;
    if (!evs->getCameraList(&cams).isOk()) return 2;
    for (const auto& c : cams) std::printf("CAMERA %s\n", c.id.c_str());
    return 0;
  }

  int64_t close_after_ms = -1;
  size_t i = 0;
  if (args[0] == "--close-after-ms") {
    if (args.size() < 2) return usage();
    close_after_ms = std::atoll(args[1].c_str());
    i = 2;
  }
  if (args.size() < i + 2) return usage();
  const int64_t run_ms = std::atoll(args[i].c_str()) * 1000;
  if (run_ms <= 0) return usage();

  std::vector<Session> sessions;
  int rc = 0;
  for (size_t k = i + 1; k < args.size(); ++k) {
    Session s;
    s.id = args[k];
    Stream empty;  // Default stream configuration of the HAL.
    if (!evs->openCamera(s.id, empty, &s.camera).isOk() || !s.camera) {
      std::fprintf(stderr, "PROBE %s open_failed\n", s.id.c_str());
      rc = 3;
      continue;
    }
    s.counter = ndk::SharedRefBase::make<Counter>(s.camera);
    if (!s.camera->startVideoStream(s.counter).isOk()) {
      std::fprintf(stderr, "PROBE %s start_failed\n", s.id.c_str());
      evs->closeCamera(s.camera);
      rc = 3;
      continue;
    }
    sessions.push_back(std::move(s));
  }

  auto stopAll = [&] {
    for (auto& s : sessions) {
      if (s.stopped) continue;
      const auto t0 = Clock::now();
      s.camera->stopVideoStream();
      const bool ok = s.counter->waitStopped(std::chrono::milliseconds(2000));
      s.stop_ms = msSince(t0);
      s.stopped = true;
      if (!ok) {
        std::fprintf(stderr, "PROBE %s stop_timeout\n", s.id.c_str());
        rc = 4;
      }
      s.counter->print(s.id, s.stop_ms);
    }
  };

  const auto start = Clock::now();
  if (close_after_ms >= 0 && close_after_ms < run_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(close_after_ms));
    stopAll();
  }
  std::this_thread::sleep_until(start + std::chrono::milliseconds(run_ms));
  stopAll();
  for (auto& s : sessions) evs->closeCamera(s.camera);
  std::fflush(stdout);
  return rc;
}
