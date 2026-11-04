# AOSP bug report draft: CarEvsService leaks a stream client on each R-while-ACTIVE, rear view stops after ~10

Draft for Google Issue Tracker (component: Android Automotive / Car framework, EVS). Written 2026-11-02 by the VIPEE ROS2AAOSBridge team; to be filed by Wendell.

---

**Title:** CarEvsService: StreamHandler client count leaks when a start request arrives while the stream is ACTIVE; startVideoStream fails permanently after ~10 reverse engagements

**Build:** AOSP `packages/services/Car` at `b8fe0e1` (25Q4-release, Android 16 QPR), `sdk_car_x86_64-bp4a-userdebug` emulator. EVS HAL: AOSP `evs-v4l2` (`packages/services/Car/cpp/evs/v4l2_hal/aidl`) behind `evsmanagerd --target hw/1`. Also reproduced with a native client that mimics CarEvsService's bookkeeping against evsmanagerd directly.

**Summary**

The native `StreamHandler` in CarEvsService counts stream clients (`mNumClients`). Every start request while the stream is already running increments the count and grows `setMaxFramesInFlight` by 10. Every stop decrements it once. The `StateMachine` can issue two starts for one stop. An R gear event while `ACTIVE` moves the service to `REQUESTED` without stopping the stream. The client's new start then increments the count again. A later D or stop in `REQUESTED` goes to `INACTIVE` without a stop request. Each such sequence leaks one client (10 buffers). The camera stays open between sessions, so the leak survives until evsmanagerd dies. After about 10 leaks, evsmanagerd requests 110 buffers. The HAL's cap is 100, so every later `startVideoStream` fails. The rear view never opens again until the EVS HAL or evsmanagerd restarts.

**Code references** (at `b8fe0e1`)

- `service/jni/evs/StreamHandler.cpp:124-151` `startStream()`: when `mRunning`, `++mNumClients` and `setMaxFramesInFlight(mNumClients * mMaxNumFramesInFlightPerClient)`. `kMaxNumFramesInFlight = 10` in `EvsServiceContext.h:218`.
- `service/jni/evs/StreamHandler.cpp:157-205` `blockingStopStream()`: `--mNumClients` once. The stream stops only when the count reaches 0.
- `service/jni/evs/EvsServiceContext.cpp:261-265`: the camera and StreamHandler stay open between sessions. They are dropped only by the evsmanagerd death handler (`~515-525`).
- `service/src/com/android/car/evs/StateMachine.java:1104-1117`: request while `SERVICE_STATE_ACTIVE` goes to `SERVICE_STATE_REQUESTED`. The running stream is not stopped and the callback stays registered.
- `StateMachine.java:1202-1226` and `:1256-1261`: start with the new token goes `REQUESTED` to `ACTIVE`, re-registers the same callback and calls `requestToStartVideoStream`. The count goes +1.
- `StateMachine.java:1002-1010`: stop or cancel while `REQUESTED` goes to `INACTIVE` without `requestToStopVideoStream`.
- evsmanagerd `cpp/evs/manager/aidl/src/VirtualCamera.cpp:359-380` and `HalCamera.cpp:113-132`: the per-client request is forwarded as the sum over clients. `evs-v4l2` `EvsV4lCamera.cpp:46` (`kMaxBuffersInFlight = 100`) rejects anything above 100 at `:551-554`.

**Steps to reproduce**

1. Configure a REARVIEW camera with an activity (`config_carEvsService`) and gear-driven EVS (`GEAR_SELECTION` or `EVS_SERVICE_REQUEST`).
2. Engage R. The rear view activity starts the stream, and the service is `ACTIVE`.
3. While still `ACTIVE`, deliver another R request. Examples: R-D-R faster than the activity tears down, an app re-requesting the rear view, or a repeated R event. The service goes `ACTIVE` to `REQUESTED`, then the activity's start goes `REQUESTED` to `ACTIVE`. The count is now 2.
4. Shift to D. The service stops once. The count is now 1, and the stream keeps running with no client.
5. Repeat steps 2-4 about 10 times.

**Expected:** every R engagement opens the rear view. After D, the stream stops and no buffers stay reserved.

**Actual:**
- `dumpsys car_service` shows `ACTIVE -> REQUESTED -> ACTIVE` followed by `ACTIVE -> INACTIVE`.
- evsmanagerd keeps the VirtualCamera `RUNNING` with `FramesAllowed 10` while CarEvsService is `INACTIVE` with no clients.
- After about 10 cycles, evsmanagerd logs `<camera>: Failed to change buffer count by 10 to 110` and the HAL logs `Rejecting buffer request in excess of internal limit`.
- CarEvsService logs "Failed to start a video stream" (error -1).
- The rear view stays black until the EVS HAL or evsmanagerd restarts.

**Impact:** on a vehicle, the rear view camera stops working after about 10 fast R-D-R sequences (parking maneuvers) until reboot. This is a safety-relevant regression for any OEM using CarEvsService for the rear view.

**Suggested fix (either):**
- In `StateMachine`, do not call `requestToStartVideoStream` again for a callback whose stream is already running when going `REQUESTED` to `ACTIVE`. Alternatively, stop the stream on `ACTIVE` to `REQUESTED`.
- In `StreamHandler`, count clients by callback identity (a set), not by a counter, so a repeated start from the same client is idempotent.
- Make `REQUESTED` to `INACTIVE` issue `requestToStopVideoStream` when a stream is running.

**Workaround (ours):** the EVS HAL clamps buffer requests above its cap, and logs a WARNING instead of failing. This keeps the rear view working but hides the leak.

**Note:** we know CarEvsService is being phased out in favor of Camera2/ACS (source.android.com/docs/automotive/camera/acs/camera2-migration). The bug still affects current releases that ship it.

---

Attachments to add when filing: logcat excerpt with `Failed to change buffer count by 10 to 110` (`/home/wendell/Projetos/vipee-verify-scratch/avm-vehicle-signals/fv/evs_leak/stuck_suite_fv4_full2.txt`, redact anything private), and the `dumpsys car_service` state history.
