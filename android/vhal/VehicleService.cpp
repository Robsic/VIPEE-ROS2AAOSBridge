#define LOG_TAG "VipeeVehicleService"

#include <DefaultVehicleHal.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <utils/Log.h>

#include "VipeeVehicleHardware.h"

using ::android::hardware::automotive::vehicle::DefaultVehicleHal;

// Same shape as
// device/generic/car/emulator/vhal_aidl/EmulatedVehicleService.cpp.
int main(int /* argc */, char* /* argv */[]) {
  if (!ABinderProcess_setThreadPoolMaxThreadCount(4)) {
    ALOGE("failed to set thread pool max thread count");
    return 1;
  }
  ABinderProcess_startThreadPool();

  auto hardware = std::make_unique<vipee::VipeeVehicleHardware>();
  std::shared_ptr<DefaultVehicleHal> vhal =
      ::ndk::SharedRefBase::make<DefaultVehicleHal>(std::move(hardware));

  binder_exception_t err = AServiceManager_addService(
      vhal->asBinder().get(),
      "android.hardware.automotive.vehicle.IVehicle/default");
  if (err != EX_NONE) {
    ALOGE("failed to register IVehicle/default, exception: %d", err);
    return 1;
  }
  ALOGI("VIPEE vehicle service ready");
  ABinderProcess_joinThreadPool();
  return 0;
}
