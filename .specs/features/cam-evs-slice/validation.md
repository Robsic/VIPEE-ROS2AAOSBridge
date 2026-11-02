# cam-evs-slice Validation

**Date**: 2026-11-02
**Spec**: `.specs/features/cam-evs-slice/spec.md`
**Diff range**: `1f55d54..cae13e3` (16 commits, f91abe4 → cae13e3)
**Verifier**: independent sub-agent (author ≠ verifier)
**Verdict**: FAIL

The core (map, JPEG ➔ YUYV, LatestSlot, CameraChannel) is well covered: its tests assert the spec outcomes and killed every core mutant. The verdict is FAIL because the sensor left 4 survivors. Two are in `ros/`: a camera map rejected with MAP_INVALID still opens the cameras (CAM-17), and decoding on the executor thread still passes the "stuck camera does not delay telemetry" test (CAM-05/CAM-08). The other two are in `V4l2Sink`, whose success path has no test. CAM-19 (camera types in `CarEvsManager`) has no assertion either.

---

## Task Completion

| Task | Status | Notes |
| ---- | ------ | ----- |
| T1–T16 | ✅ Done | All `[x]` in `tasks.md`. During this run someone other than the Verifier edited T16's last Done-when in the working tree (unstaged; not part of this report) |

---

## Gate Check

- **Host build gate**: `cmake --preset default && cmake --build --preset default && ctest --preset default` ➔ 120/120 passed, 0 skipped (run by the Verifier).
- **ROS gate**: `tools/jazzy/run.sh /bridge/tools/jazzy/test-ros.sh` ➔ 3/3 ctest entries passed (`bridge_node_rostest`, `camera_bridge_rostest`, `unreadable_map_test`), run by the Verifier.
- **Lint**: `clang-format --dry-run --Werror` passes on all 24 touched C++ files outside the vendored HAL. `clang-tidy -p build` reports 0 findings on the 9 host-buildable files (core src/test, `v4l2_sink.cpp`, `v4l2_sink_test.cpp`).
- **Test count**: `TEST`/`TEST_F` across the suites went from 85 at `1f55d54` to 131 at HEAD (+46). No test was removed. The map-version test changed from rejecting 2 to rejecting 1 and 3, as T5 requires.
- **Emulator** (`tools/emu/test-cam.sh`, `test-e2e.sh`): the Verifier did not run these. The phase-4 worker reported, and this report only cites, that the bag gave 527–529 frames in 60 s per camera with p95 ≤ 11.4 ms, STALE at 600 ms, drops with the right reasons, DEVICE_UNAVAILABLE, install/remove, test-e2e at 121 ms / 561 ms, and 0 avc denials.

---

## Spec-Anchored Acceptance Criteria

| ID | Spec-defined outcome | Evidence (`file:line` + assertion) | Result |
| -- | -------------------- | ---------------------------------- | ------ |
| CAM-01 | JPEG 1280×720 on `<pos>` reaches the mapped EVS camera in 1280×720 | `core/test/camera_channel_test.cpp:94` `EXPECT_EQ(sink_.frames[0], expected)` + size `kW*kH*2`. `ros/vipee_bridge/test/camera_bridge_rostest.cpp:193` `EXPECT_EQ(vector{kFrameBytes}, sinks_["rear"]->Lens())`, other sinks 0. Emulator: `tools/emu/test-cam.sh:256` `check_rate` width/height = 1280×720 (reported) | ✅ PASS |
| CAM-02 | ≥ 8 frames/s per camera on a `CarEvsManager` client | `tools/emu/test-cam.sh:266` `check_rate` `n ≥ 8*BAG_S` per device (reported). The test uses the evsmanagerd probe, not `CarEvsManager` (design decision; AVM is a manual success criterion) | ✅ PASS (emulator, reported) |
| CAM-03 | ≤ 100 ms rx ➔ device, measured in the ROS process | `tools/emu/test-cam.sh:273-276` `max p95_ms <= 100` (reported). p95 math: `core/test/camera_channel_test.cpp:223` `EXPECT_GE(p95, 1900ms)`/`EXPECT_LT(…, 2000ms)`. The SPEC_DEVIATION at `core/src/camera_channel.cpp:65` adds decode+write time. That is stricter than the design and matches the AC wording ("até o quadro estar disponível no dispositivo") | ✅ PASS |
| CAM-04 | Each frame delivered at most once, no repeat when no new frame | `core/test/camera_channel_test.cpp:107` `EXPECT_EQ(sink_.frames.size(), 1u)` after 40 ticks. `camera_bridge_rostest.cpp:197-198` exactly one write after 400 ms. HAL fork: `tools/emu/test-cam.sh:162` `last_frame_ms_ago >= 2000` (reported) | ✅ PASS |
| CAM-05 | ct-speed-slice latency ≤ 200 ms and stale ≤ 600 ms with 4 cameras on | `tools/emu/test-e2e.sh:29-31,162` cameras always `ok` + `cam_stats` (reported 121 ms / 561 ms). In-process isolation test `camera_bridge_rostest.cpp:236` does **not** discriminate: mutant R3 (decode on the executor) survives | ⚠️ PASS on emulator; weak ROS test (Gap 2) |
| CAM-06 | > stale_ms without valid frame ➔ stop delivering, `CAM <pos> STALE` | `core/test/camera_channel_test.cpp:113` no STALE at exactly 500 ms, `"CAM front STALE"` at 500 ms + 1 ns. `camera_bridge_rostest.cpp:206` STALE and sink count stays 1. `test-cam.sh:281` 0 frames and STALE ×4 (reported) | ✅ PASS |
| CAM-07 | Valid frame in STALE ➔ delivered again, `CAM <pos> LIVE`, no reopen | `core/test/camera_channel_test.cpp:122` lines `{LIVE, STALE, LIVE}`, 2 frames. `camera_bridge_rostest.cpp:214-217`. `test-cam.sh:298-308` probe left open across pause (reported) | ✅ PASS |
| CAM-08 | A stopped camera does not interrupt the others or the VHAL | `test-cam.sh:337` right camera has no DROP/STALE and keeps ≥ 8/s. `test-cam.sh:360` VHAL speed with front unavailable (reported). `camera_bridge_rostest.cpp:236` is weak (R3 survives) | ⚠️ PASS on emulator; weak ROS test (Gap 2) |
| CAM-09 | Other size ➔ drop, no resize, `FRAME_SIZE_MISMATCH` | `core/test/jpeg_yuyv_test.cpp:108` `kFrameSizeMismatch` and output untouched `{7,7,7}`. `camera_channel_test.cpp:157-159` log line | ✅ PASS |
| CAM-10 | Non-JPEG `format` ➔ `FORMAT_UNSUPPORTED` | `core/test/jpeg_yuyv_test.cpp:115` `png` and `""` ➔ `kFormatUnsupported`; `:100` `rgb8; jpeg compressed bgr8` accepted | ✅ PASS |
| CAM-11 | Decode failure ➔ `DECODE_ERROR` | `core/test/jpeg_yuyv_test.cpp:124` (truncated), `:132` (garbage, empty), `:139` (4:4:4) ➔ `kDecodeError` | ✅ PASS |
| CAM-12 | `CAM <pos> DROP <reason>` once per reason change | `core/test/camera_channel_test.cpp:146` exactly 3 lines for 6 bad frames, then 5 after a valid frame and a repeat. `test-cam.sh:333` one DROP per camera (reported) | ✅ PASS |
| CAM-13 | Dropped frame counts as absent for staleness | `core/test/camera_channel_test.cpp:138` PNG at 400 ms, STALE at 500 ms + 1 | ✅ PASS |
| CAM-14 | Device fails ➔ `DEVICE_UNAVAILABLE`, retry every 1 s, process alive | `core/test/camera_channel_test.cpp:167` opens = 1 at 1099 ms, 2 at 1100 ms, `DEVICE_UNAVAILABLE` once, `DEVICE_OK`. `:199` open failure. `test-cam.sh:363-365` same pid, single line (reported) | ✅ PASS |
| CAM-15 | Others and VHAL keep running while one device is down | `tools/emu/test-cam.sh:359-362` 3 cameras ≥ 8/s + VHAL 10 m/s (reported) | ✅ PASS (emulator, reported) |
| CAM-16 | Topic, EVS view, width, height, stale_ms read from the selected map | `ros/vipee_bridge/test/bridge_node_rostest.cpp:244` both real YAML maps ➔ 4 cameras with exact topic, view, device, 1280, 720, 500 | ✅ PASS |
| CAM-17 | Invalid camera section ➔ **no camera opens**, `CAM MAP_INVALID`, VHAL as before | `bridge_node_rostest.cpp:293` asserts `CAM MAP_INVALID DUPLICATE_VIEW` and telemetry. `core/test/vehicle_map_test.cpp:147-199` one test per error. Nothing asserts "no camera opens": mutant R1 (CameraBridge built despite the error) survives | ❌ GAP (Gap 1) |
| CAM-18 | No `cameras` ➔ telemetry only, no error | `bridge_node_rostest.cpp:281` empty list, `kNone`, no CAM log. `core/test/vehicle_map_test.cpp:133` | ✅ PASS |
| CAM-19 | After install the 4 cameras are available to `CarEvsManager` as FRONTVIEW/REARVIEW/LEFTVIEW/RIGHTVIEW | `tools/emu/test-cam.sh:380-384` only checks that the probe lists `/dev/video10..13` on evsmanagerd. No test asserts the `set-camera-id` mapping (`tools/jazzy/install-vendor.sh:56-58`) | ❌ GAP (Gap 3) |
| CAM-20 | Remove ➔ previous HAL and camera config | `tools/emu/test-cam.sh:375-377` `CAMERA cam_front` listed after `evs-remove` (reported). Only one of the 4 MP4 cameras is checked | ⚠️ Partial (Gap 5) |
| CAM-21 | Enforcing, 0 `avc: denied` in bridge/HAL domains | `tools/emu/test-cam.sh:77-82` `avc_check` in every step; `test-e2e.sh` (reported) | ✅ PASS (reported) |
| CAM-22 | Bag loop (stamp goes back) keeps delivering | `core/test/camera_channel_test.cpp:212` 2 frames with rx going back. `test-cam.sh:256` 60 s over a 24 s bag ≈ 2 loops (reported) | ✅ PASS |
| CAM-23 | Two messages before the write ➔ only the newest, silently | `core/test/latest_slot_test.cpp:177` `take == 2`. `camera_bridge_rostest.cpp:220` burst ➔ last luma 200, < 5 writes | ✅ PASS |
| CAM-24 | No EVS client ➔ keeps accepting without accumulating memory | `core/test/latest_slot_test.cpp:213` older `shared_ptr`s expire, one value kept | ✅ PASS |

**Status**: ❌ Gaps present (CAM-17, CAM-19); CAM-05/CAM-08 rely on the emulator because the ROS test is weak; CAM-20 is partial.

### Spec-precision notes

- CAM-12 "uma vez, não a cada quadro": the implementation logs the same reason again after a valid frame in between (`camera_channel_test.cpp:160-164`). That is a reasonable reading, and the test pins it.
- CAM-02 says "cliente `CarEvsManager`", but the automated test uses the native evsmanagerd probe. Design.md sends `CarEvsManager` itself to the manual AVM success criterion. This is acknowledged rather than hidden, but no automated test covers the Java layer.

---

## Discrimination Sensor

Run in a temporary `git worktree` under the session scratchpad. ROS mutants were built in a separate container build dir (`/ws/ros_test/build_bridge_mut`, deleted afterwards). Worktree removed. The real tree's `git status --porcelain` matched the clean baseline for every file the sensor touched; the only difference is the unrelated T16 `tasks.md` edit by another actor noted above.

| # | File:line | Mutation | Killed? |
| - | --------- | -------- | ------- |
| M1 | `core/src/camera_channel.cpp:74` | STALE `>` ➔ `>=` | ✅ `StaleOnlyAfterMoreThanStaleMs` |
| M2 | `core/include/vipee/latest_slot.h:37` | `put` keeps the older value | ✅ `TwoPutsBeforeTakeDeliverOnlyTheNewest`, `PutsWithoutTakeKeepOneValue` |
| M3 | `core/src/camera_channel.cpp:49` | DROP logged on every frame | ✅ `DropIsLoggedOncePerReasonChange` |
| M4 | `core/src/jpeg_yuyv.cpp:53` | size mismatch ignored (decoded into map size) | ✅ `DropIsLoggedOncePerReasonChange`, `StatsCount…` |
| M5 | `core/src/jpeg_yuyv.cpp:68` | decode error ignored | ✅ `TruncatedJpegIsDecodeError` |
| M6 | `core/src/camera_channel.cpp:72` | `tick` rewrites the last frame (frame delivered twice) | ✅ `DroppedFrameDoesNotRenewTheDeadline`, `WriteFailure…` |
| M7 | `core/src/vehicle_map.cpp:38` | duplicate EVS view accepted | ✅ `RepeatedViewIsRejected` |
| M8 | `core/src/camera_channel.cpp:45` | dropped frame renews the deadline | ✅ `DroppedFrameDoesNotRenewTheDeadline` |
| M9 | `core/src/camera_channel.cpp:97` | reopen without 1 s back-off | ✅ `WriteFailure…`, `DeviceThatDoesNotOpen…` |
| M10 | `ros/vipee_bridge/src/v4l2_sink.cpp:147` | partial `write()` accepted (`n < 0` only) | ❌ Survived |
| M11 | `ros/vipee_bridge/src/v4l2_sink.cpp:132-133` | driver-adjusted width/height accepted | ❌ Survived |
| M12 | `core/src/camera_channel.cpp:55` | no `LIVE` log on STALE ➔ LIVE | ✅ `NextValidFrameAfterStaleIsLiveAndWritten` |
| R1 | `ros/vipee_bridge/src/bridge_node.cpp:128` | CameraBridge built despite `CAM MAP_INVALID` | ❌ Survived |
| R2 | `ros/vipee_bridge/src/camera_bridge.cpp:44` | camera thread never calls `tick` | ✅ `SilencedTopicGoesStale…` |
| R3 | `ros/vipee_bridge/src/camera_bridge.cpp:77` | decode+write on the executor callback instead of the camera thread | ❌ Survived |
| R4 | `ros/vipee_bridge/src/bridge_node.cpp:102` | width read from `height` | ✅ `BothRealMapsLoadFourCameras…` |
| R5 | `ros/vipee_bridge/src/camera_bridge.cpp:75` | every camera subscribes to the first topic | ✅ `JpegReachesOnlyTheRightCameraSink…`, `Burst…` |

**Sensor depth**: expanded (17 mutants; data-integrity path: a frozen or wrong frame is the spec's worst case).
**Result**: 13/17 killed. FAIL ❌ (survivors M10, M11, R1, R3).

R3 survives because `StuckCameraDoesNotDelayTelemetry` (`camera_bridge_rostest.cpp:243-252`) publishes the camera frame and then spins for 200 ms (`SpinFor(200)`). With decoding on the executor, the 1.5 s write runs inside that `SpinFor`, before `start` is taken, so the telemetry measurement never sees the stall.

The forked HAL (`android/evs_hal`, 4 `VIPEE:` patches) is not mutation-tested: it is AOSP code with no host test seam. Its only coverage is `test-cam.sh stall`/`platform` (`tools/emu/test-cam.sh:102,143`; reported). Each patch has a matching assertion there: close with no writer `< 1000 ms` (poll), late writer gives frames (deferred STREAMON), listed before the bridge starts (keep_format), and 0 frames from a stale loopback buffer (timestamp drop).

---

## API Correctness (context7)

- **TurboJPEG 3** (`/libjpeg-turbo/libjpeg-turbo`): `tj3Init(TJINIT_DECOMPRESS)`, `tj3DecompressHeader`, `tj3Get(TJPARAM_JPEGWIDTH/JPEGHEIGHT/SUBSAMP)` and `tj3DecompressToYUVPlanes8(handle, buf, size, planes, strides)` match the documented signatures. A libjpeg warning (corrupt or truncated data) makes the call return -1, so treating any non-zero result as `DECODE_ERROR` is correct (`core/src/jpeg_yuyv.cpp:67-70`). Chroma strides of `width/2` match the 4:2:0/4:2:2 plane widths for even widths, which `jpeg_yuyv.cpp:56` enforces.
- **rclcpp (Jazzy)**: `rclcpp::SensorDataQoS().keep_last(1)` with a `ConstSharedPtr` callback is valid. The default callback group is MutuallyExclusive, which is why decoding has to stay off the executor (R3).
- **V4L2**: `VIDIOC_S_FMT` on `V4L2_BUF_TYPE_VIDEO_OUTPUT` with `V4L2_PIX_FMT_YUYV`, and checking the format the driver returns, follows `linux/videodev2.h`. The emulator run exercises it with `v4l2loopback` (reported). The host never reaches the success path (M10/M11).

---

## Code Quality

| Principle | Status |
| --------- | ------ |
| Minimum code / surgical | ✅ Core is small; HAL fork limited to 4 marked patches (AD-016) |
| Matches patterns | ✅ Logger-per-transition, map validation and fakes of the project's own interfaces follow ct-speed-slice |
| SPEC_DEVIATION markers | ✅ 2, both justified: `core/src/camera_channel.cpp:65` (latency includes decode+write, stricter) and `ros/vipee_bridge/src/camera_bridge.cpp:29` (tick/STATS on the camera thread, so the non-thread-safe channel has one owner) |
| Spec-anchored outcome check | ⚠️ CAM-17 and CAM-19 are not asserted. The CAM-05/08 ROS test does not discriminate |
| Per-layer coverage | ⚠️ Core is 1:1. ROS is missing the CAM-17 "no camera opens" check. `V4l2Sink` success path has no test |
| Every test maps to an AC | ✅ |
| Guidelines: `AGENTS.md` §3, §3.1, §4 | ✅ Lint clean, Doxygen on the public headers, no mocks of rclcpp/HAL. T10/T12 tests were written without a separate RED run. The sensor confirms T12's STALE, routing and newest-frame tests are discriminating (R2, R5 killed); T12's isolation test and T10's MAP_INVALID test are not (R3, R1) |

---

## Fix Plans

### Fix 1 (Major): CAM-17 "no camera opens" is not asserted

- **Root cause**: `bridge_node_rostest.cpp:293` checks the log line and telemetry, but passes no sink factory and never checks whether a CameraBridge or sink was created.
- **Fix task**: in `DuplicateEvsViewLogsCamMapInvalidAndKeepsTelemetry`, pass a `SinkFactory` that counts calls, and assert 0 calls and no `DEVICE_UNAVAILABLE`/`LIVE` lines after ~200 ms of spinning. Must kill R1 (`bridge_node.cpp:128` condition without `camera_error_`).

### Fix 2 (Major): the stuck-camera isolation test does not discriminate (CAM-05, CAM-08)

- **Root cause**: `camera_bridge_rostest.cpp:243-252` takes `start` after `SpinFor(200)`, so a stall on the executor is spent before the measurement.
- **Fix task**: publish the camera frame and the telemetry back to back, take `start` before both, and assert that speed reaches the socket in < 500 ms while the sink write (1.5 s) is still running. Must kill R3 (decode in the subscription callback).

### Fix 3 (Minor): CAM-19 camera types are not asserted

- **Fix task**: in `tools/emu/test-cam.sh step_install` (and `platform`), assert the `CarEvsService` type ➔ device mapping, for example with `dumpsys car_service` (CarEvsService section) or a `cmd car_service` query showing FRONTVIEW=/dev/video10 … RIGHTVIEW=/dev/video13. Also remove or implement the claim at `tools/jazzy/install-vendor.sh:35-36` that test-cam.sh reapplies `set-camera-id`.

### Fix 4 (Minor): V4l2Sink success path is untested (M10, M11)

- **Fix task**: either extract the format-acceptance and write-length checks into small functions with host unit tests (YUYV and size as requested ➔ accept; adjusted width ➔ reject; short write ➔ failure and close), or add a `test-cam.sh` assertion that a short write or a different-size device gives `DEVICE_UNAVAILABLE`. Must kill M10 and M11.

### Fix 5 (Cosmetic): CAM-20 checks only one MP4 camera

- **Fix task**: `test-cam.sh step_install` should check that all 4 `cam_*` are listed after `evs-remove`, and that `init.svc.vendor.evs-v4l2` is not running.

---

## Requirement Traceability Update

| Requirement | New Status |
| ----------- | ---------- |
| CAM-01..04, 06, 07, 09..16, 18, 21..24 | ✅ Verified |
| CAM-05, CAM-08 | ⚠️ Verified on emulator (reported); ROS test needs Fix 2 |
| CAM-17 | ❌ Needs Fix 1 |
| CAM-19 | ❌ Needs Fix 3 |
| CAM-20 | ⚠️ Partial (Fix 5) |

---

## Summary

**Overall**: ❌ Not Ready

**Spec-anchored check**: 19/24 ACs match the spec outcome. 2 gaps (CAM-17, CAM-19), 2 weak (CAM-05, CAM-08), 1 partial (CAM-20).
**Sensor**: 13/17 killed (survivors M10, M11, R1, R3).
**Gate**: host 120/120, ROS 3/3, lint clean.

**What works**: map parsing and validation, JPEG ➔ YUYV with fail-loud drop reasons, single-slot mailbox, STALE/LIVE/DROP/DEVICE state machine, topic routing, newest-frame delivery, and the HAL fork as exercised on the emulator (reported).

**Next steps**: Fixes 1 and 2 (ROS tests only, no production change expected), then Fixes 3–5. After that, re-dispatch the Verifier (iteration 1 of 3).
