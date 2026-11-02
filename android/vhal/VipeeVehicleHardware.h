/**
 * @file VipeeVehicleHardware.h
 * @brief Thin IVehicleHardware fed by the ROS bridge over
 *        /dev/socket/vipee_vhal (AD-013).
 */
#ifndef VIPEE_VIPEE_VEHICLE_HARDWARE_H_
#define VIPEE_VIPEE_VEHICLE_HARDWARE_H_

#include <IVehicleHardware.h>
#include <VehicleObjectPool.h>
#include <VehiclePropertyStore.h>
#include <android-base/thread_annotations.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "vipee/vhal_ingest.h"

namespace vipee {

namespace aidlvhal = ::aidl::android::hardware::automotive::vehicle;
using ::android::hardware::automotive::vehicle::DumpResult;
using ::android::hardware::automotive::vehicle::IVehicleHardware;

/**
 * @brief Serves PERF_VEHICLE_SPEED, GEAR_SELECTION, CURRENT_GEAR and
 *        PARKING_BRAKE_ON, all READ, from the bridge socket.
 *
 * Holds the last value and status of each property in a VehiclePropertyStore
 * and forwards change events. The bridge decides value and status; this class
 * only stores them, forwards every speed sample (the client decimates) and
 * marks the four
 * UNAVAILABLE when the peer closes or is silent for kWatchdogNs (300 ms).
 *
 * @note No platform properties are declared (AD-014).
 */
class VipeeVehicleHardware final : public IVehicleHardware {
 public:
  /// @brief Default config path (prebuilt_etc vipee_properties.json).
  static constexpr const char* kConfigPath =
      "/vendor/etc/vipee/vipee_properties.json";
  /// @brief init socket name (`socket vipee_vhal seqpacket ...` in the .rc).
  static constexpr const char* kSocketName = "vipee_vhal";

  /**
   * @brief Loads the configs, marks the four UNAVAILABLE and starts the
   *        socket thread.
   * @param configPath JSON in the DefaultProperties.json format.
   * @note Aborts if the config cannot be loaded or the init socket is missing
   *       (fail-loud: a VHAL without its source must not look healthy).
   */
  explicit VipeeVehicleHardware(const std::string& configPath = kConfigPath);
  ~VipeeVehicleHardware() override;

  std::vector<aidlvhal::VehiclePropConfig> getAllPropertyConfigs()
      const override;
  /// @return ACCESS_DENIED for every request (all properties are READ).
  aidlvhal::StatusCode setValues(
      std::shared_ptr<const SetValuesCallback> callback,
      const std::vector<aidlvhal::SetValueRequest>& requests) override;
  /// @note Stored status AVAILABLE returns the value; UNAVAILABLE returns
  ///       NOT_AVAILABLE and ERROR returns INTERNAL_ERROR.
  aidlvhal::StatusCode getValues(
      std::shared_ptr<const GetValuesCallback> callback,
      const std::vector<aidlvhal::GetValueRequest>& requests) const override;
  DumpResult dump(const std::vector<std::string>& options) override;
  /// @return OK while the bridge peer is connected and alive, else TRY_AGAIN.
  aidlvhal::StatusCode checkHealth() override;
  void registerOnPropertyChangeEvent(
      std::unique_ptr<const PropertyChangeCallback> callback) override;
  void registerOnPropertySetErrorEvent(
      std::unique_ptr<const PropertySetErrorCallback> callback) override;
  /// @note Records the PERF_VEHICLE_SPEED rate, shown in the dump.
  aidlvhal::StatusCode subscribe(aidlvhal::SubscribeOptions options) override;
  aidlvhal::StatusCode unsubscribe(int32_t propId, int32_t areaId) override;
  aidlvhal::StatusCode updateSampleRate(int32_t propId, int32_t areaId,
                                        float sampleRate) override;

 private:
  void socketLoop();
  void handleDatagram(const uint8_t* data, size_t len, int64_t now);
  void closePeer(bool marked, int64_t now);
  void writeRecord(const WireRecord& r);
  void markAllUnavailable(int64_t ts);
  void setSpeedRate(int32_t propId, float hz);

  std::shared_ptr<
      ::android::hardware::automotive::vehicle::VehiclePropValuePool>
      mPool;
  std::unique_ptr<
      ::android::hardware::automotive::vehicle::VehiclePropertyStore>
      mStore;
  std::vector<aidlvhal::VehiclePropConfig> mConfigs;

  std::mutex mCallbackLock;
  std::unique_ptr<const PropertyChangeCallback> mOnChange
      GUARDED_BY(mCallbackLock);

  std::mutex mIngestLock;
  VhalIngest mIngest GUARDED_BY(mIngestLock);

  int mListenFd = -1;
  int mPeerFd = -1;          // socket thread only
  int mWakeFd = -1;          // eventfd to stop the socket thread
  bool mLoggedDrop = false;  // socket thread only; one drop log per peer
  std::atomic<bool> mPeerAlive{false};
  std::atomic<uint64_t> mSpeedEvents{0};  // emitted speed events, for dump
  std::thread mThread;
};

}  // namespace vipee

#endif  // VIPEE_VIPEE_VEHICLE_HARDWARE_H_
