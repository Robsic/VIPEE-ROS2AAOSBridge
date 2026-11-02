#define LOG_TAG "VipeeVehicleHardware"

#include "VipeeVehicleHardware.h"

#include <JsonConfigLoader.h>
#include <VehicleUtils.h>
#include <cutils/sockets.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utils/Log.h>
#include <utils/SystemClock.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace vipee {

using aidlvhal::GetValueResult;
using aidlvhal::SetValueResult;
using aidlvhal::StatusCode;
using aidlvhal::VehiclePropertyStatus;
using ::android::elapsedRealtimeNano;
using ::android::hardware::automotive::vehicle::getErrorCode;
using ::android::hardware::automotive::vehicle::JsonConfigLoader;
using ::android::hardware::automotive::vehicle::VehiclePropertyStore;
using ::android::hardware::automotive::vehicle::VehiclePropValuePool;

namespace {

constexpr int kPollMs = 50;  // watchdog resolution, well under 300 ms

const char* dropName(DropReason r) {
  switch (r) {
    case DropReason::kBadSize:
      return "BAD_SIZE";
    case DropReason::kUnknownVersion:
      return "UNKNOWN_VERSION";
    case DropReason::kUnknownKind:
      return "UNKNOWN_KIND";
    case DropReason::kUnknownProp:
      return "UNKNOWN_PROP";
    case DropReason::kBadStatus:
      return "BAD_STATUS";
    case DropReason::kNone:
      break;
  }
  return "NONE";
}

}  // namespace

VipeeVehicleHardware::VipeeVehicleHardware(const std::string& configPath)
    : mPool(std::make_shared<VehiclePropValuePool>()),
      mStore(std::make_unique<VehiclePropertyStore>(mPool)) {
  JsonConfigLoader loader;
  auto configs = loader.loadPropConfig(configPath);
  if (!configs.ok()) {
    LOG_ALWAYS_FATAL("cannot load %s: %s", configPath.c_str(),
                     configs.error().message().c_str());
  }
  for (const auto& [propId, decl] : *configs) {
    mConfigs.push_back(decl.config);
    mStore->registerProperty(decl.config);
  }
  mStore->setOnValuesChangeCallback(
      [this](std::vector<aidlvhal::VehiclePropValue> values) {
        std::scoped_lock<std::mutex> lock(mCallbackLock);
        if (mOnChange) (*mOnChange)(std::move(values));
      });
  markAllUnavailable(elapsedRealtimeNano());  // NO_DATA until the bridge talks

  mListenFd = android_get_control_socket(kSocketName);
  if (mListenFd < 0 || listen(mListenFd, 2) != 0) {
    LOG_ALWAYS_FATAL("init socket %s missing or not listenable", kSocketName);
  }
  mWakeFd = eventfd(0, EFD_CLOEXEC);
  mThread = std::thread([this] { socketLoop(); });
}

VipeeVehicleHardware::~VipeeVehicleHardware() {
  uint64_t one = 1;
  if (write(mWakeFd, &one, sizeof one) < 0) ALOGE("eventfd write failed");
  if (mThread.joinable()) mThread.join();
  if (mPeerFd >= 0) close(mPeerFd);
  close(mWakeFd);
}

void VipeeVehicleHardware::socketLoop() {
  for (;;) {
    pollfd fds[3] = {
        {mWakeFd, POLLIN, 0}, {mListenFd, POLLIN, 0}, {mPeerFd, POLLIN, 0}};
    const int n = poll(fds, mPeerFd >= 0 ? 3 : 2, kPollMs);
    if (n < 0 && errno != EINTR) ALOGE("poll: %s", strerror(errno));
    if (fds[0].revents) return;
    if (n > 0 && (fds[1].revents & POLLIN)) {
      const int fd = accept4(mListenFd, nullptr, nullptr, SOCK_CLOEXEC);
      if (fd >= 0) {
        // A new bridge process replaces a stale one.
        if (mPeerFd >= 0) closePeer(/*marked=*/false, elapsedRealtimeNano());
        mPeerFd = fd;
        mLoggedDrop = false;
        std::scoped_lock<std::mutex> lock(mIngestLock);
        mIngest.onConnect(elapsedRealtimeNano());
        mPeerAlive = true;
        ALOGI("bridge connected");
      }
    }
    if (n > 0 && mPeerFd >= 0 && fds[2].fd == mPeerFd && fds[2].revents) {
      uint8_t buf[64];
      const ssize_t len = recv(mPeerFd, buf, sizeof buf, MSG_DONTWAIT);
      if (len > 0) {
        handleDatagram(buf, static_cast<size_t>(len), elapsedRealtimeNano());
      } else if (len == 0 || (errno != EAGAIN && errno != EINTR)) {
        ALOGW("bridge disconnected");
        closePeer(/*marked=*/false, elapsedRealtimeNano());
      }
    }
    bool expired;
    {
      std::scoped_lock<std::mutex> lock(mIngestLock);
      expired = mIngest.expire(elapsedRealtimeNano());
    }
    if (expired) {
      ALOGW("bridge silent > 300 ms: all UNAVAILABLE, dropping peer");
      closePeer(/*marked=*/true, 0);
    }
  }
}

void VipeeVehicleHardware::handleDatagram(const uint8_t* data, size_t len,
                                          int64_t now) {
  IngestResult r;
  {
    std::scoped_lock<std::mutex> lock(mIngestLock);
    r = mIngest.onDatagram(data, len, now);
  }
  if (r.action == IngestAction::kDrop) {
    if (!mLoggedDrop) ALOGE("dropped record: %s", dropName(r.drop));
    mLoggedDrop = true;
  } else if (r.action == IngestAction::kWrite) {
    writeRecord(r.record, r.emit);
  }
}

// marked: the watchdog already wrote the markers; otherwise mark now.
void VipeeVehicleHardware::closePeer(bool marked, int64_t now) {
  if (mPeerFd >= 0) close(mPeerFd);
  mPeerFd = -1;
  mPeerAlive = false;
  int64_t floor;
  {
    std::scoped_lock<std::mutex> lock(mIngestLock);
    if (!marked && !mIngest.onDisconnect(now)) return;
    floor = mIngest.floorNs();
  }
  markAllUnavailable(floor);
}

void VipeeVehicleHardware::writeRecord(const WireRecord& r, bool emit) {
  auto v = r.prop_id == kPropPerfVehicleSpeed
               ? mPool->obtainFloat(r.float_value)
               : mPool->obtainInt32(r.int_value);
  v->prop = r.prop_id;
  v->areaId = 0;
  v->timestamp = r.timestamp_ns;
  v->status = static_cast<VehiclePropertyStatus>(r.status);
  // Speed is CONTINUOUS: every emitted sample is an event. The others are
  // ON_CHANGE: the store suppresses repeats (CTS-08).
  const auto mode = !emit ? VehiclePropertyStore::EventMode::NEVER
                    : r.prop_id == kPropPerfVehicleSpeed
                        ? VehiclePropertyStore::EventMode::ALWAYS
                        : VehiclePropertyStore::EventMode::ON_VALUE_CHANGE;
  if (emit && r.prop_id == kPropPerfVehicleSpeed) ++mSpeedEvents;
  if (auto res = mStore->writeValue(std::move(v), /*updateStatus=*/true, mode);
      !res.ok()) {
    ALOGE("store rejected prop 0x%x: %s", r.prop_id,
          res.error().message().c_str());
  }
}

void VipeeVehicleHardware::markAllUnavailable(int64_t ts) {
  for (int32_t prop : {kPropPerfVehicleSpeed, kPropGearSelection,
                       kPropCurrentGear, kPropParkingBrakeOn}) {
    WireRecord r{};
    r.prop_id = prop;
    r.status = static_cast<int32_t>(VehiclePropertyStatus::UNAVAILABLE);
    r.timestamp_ns = ts;
    writeRecord(r, /*emit=*/true);
  }
}

std::vector<aidlvhal::VehiclePropConfig>
VipeeVehicleHardware::getAllPropertyConfigs() const {
  return mConfigs;
}

StatusCode VipeeVehicleHardware::setValues(
    std::shared_ptr<const SetValuesCallback> callback,
    const std::vector<aidlvhal::SetValueRequest>& requests) {
  std::vector<SetValueResult> results;
  for (const auto& req : requests) {
    results.push_back(
        {.requestId = req.requestId, .status = StatusCode::ACCESS_DENIED});
  }
  (*callback)(std::move(results));
  return StatusCode::OK;
}

StatusCode VipeeVehicleHardware::getValues(
    std::shared_ptr<const GetValuesCallback> callback,
    const std::vector<aidlvhal::GetValueRequest>& requests) const {
  std::vector<GetValueResult> results;
  for (const auto& req : requests) {
    GetValueResult out{.requestId = req.requestId};
    auto read = mStore->readValue(req.prop.prop, req.prop.areaId);
    if (!read.ok()) {
      out.status = getErrorCode(read);
    } else if ((*read)->status == VehiclePropertyStatus::UNAVAILABLE) {
      out.status = StatusCode::NOT_AVAILABLE;
    } else if ((*read)->status == VehiclePropertyStatus::ERROR) {
      out.status = StatusCode::INTERNAL_ERROR;
    } else {
      out.status = StatusCode::OK;
      out.prop = **read;
    }
    results.push_back(std::move(out));
  }
  (*callback)(std::move(results));
  return StatusCode::OK;
}

DumpResult VipeeVehicleHardware::dump(const std::vector<std::string>&) {
  std::ostringstream s;
  float hz;
  {
    std::scoped_lock<std::mutex> lock(mIngestLock);
    hz = mIngest.speedRateHz();
  }
  s << "VipeeVehicleHardware: bridge " << (mPeerAlive ? "alive" : "absent")
    << "\nspeed rate Hz: " << hz << "\nspeed events: " << mSpeedEvents << "\n";
  for (const auto& v : mStore->readAllValues()) s << v->toString() << "\n";
  return {.callerShouldDumpState = true, .buffer = s.str()};
}

StatusCode VipeeVehicleHardware::checkHealth() {
  return mPeerAlive ? StatusCode::OK : StatusCode::TRY_AGAIN;
}

void VipeeVehicleHardware::registerOnPropertyChangeEvent(
    std::unique_ptr<const PropertyChangeCallback> callback) {
  std::scoped_lock<std::mutex> lock(mCallbackLock);
  mOnChange = std::move(callback);
}

void VipeeVehicleHardware::registerOnPropertySetErrorEvent(
    std::unique_ptr<const PropertySetErrorCallback>) {
  // All properties are READ: no asynchronous set errors exist.
}

void VipeeVehicleHardware::setSpeedRate(int32_t propId, float hz) {
  if (propId != kPropPerfVehicleSpeed) return;
  std::scoped_lock<std::mutex> lock(mIngestLock);
  mIngest.setSpeedRateHz(hz);
}

StatusCode VipeeVehicleHardware::subscribe(aidlvhal::SubscribeOptions options) {
  setSpeedRate(options.propId, options.sampleRate);
  return StatusCode::OK;
}

StatusCode VipeeVehicleHardware::unsubscribe(int32_t propId, int32_t) {
  setSpeedRate(propId, 0.0f);
  return StatusCode::OK;
}

StatusCode VipeeVehicleHardware::updateSampleRate(int32_t propId, int32_t,
                                                  float sampleRate) {
  setSpeedRate(propId, sampleRate);
  return StatusCode::OK;
}

}  // namespace vipee
