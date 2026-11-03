# cam-evs-slice Validation

**Date**: 2026-11-02
**Spec**: `.specs/features/cam-evs-slice/spec.md`
**Diff range**: `1f55d54..60ecbaf` (21 commits; cycle-2 fixes `bbc8091..60ecbaf`)
**Verifier**: independent sub-agent, cycle 2 (author ≠ verifier; did not write the code or the cycle-1 report)
**Verdict**: FAIL

All 5 cycle-1 gaps are closed. Each fix test asserts the spec outcome, and the 4 cycle-1 survivors (R1, R3, M10, M11) are now killed. The verdict stays FAIL for two new findings. Both are test-only and Minor:

1. `SilencedTopicGoesStaleWithoutNewFramesThenLive` failed once in about 85 runs. It races the camera thread: the channel logs `LIVE` before it calls `write()`.
2. New mutant N7 survives: the camera loop skips `tick()` after a frame. Under N7, a camera fed invalid frames faster than the 50 ms tick period never goes `STALE` (CAM-13 at the ROS layer).

No production defect was found.

---

## Cycle History

| Cycle | Commit | Verdict | Gaps / survivors |
| ----- | ------ | ------- | ---------------- |
| 1 | `1bf5f6e` | FAIL | CAM-17 not asserted (R1). CAM-05/08 ROS test not discriminating (R3). V4l2Sink success path untested (M10, M11). CAM-19 not asserted. CAM-20 partial |
| 2 | this report | FAIL | Cycle-1 gaps all closed. New: flaky race at `camera_bridge_rostest.cpp:219`; survivor N7 (`camera_bridge.cpp:44`) |

Fix→re-verify iteration 2 of 3. One more FAIL escalates to the user.

---

## Task Completion

| Task | Status | Notes |
| ---- | ------ | ----- |
| T1–T16 | ✅ Done | All `[x]`. The working tree has an unstaged, pre-existing edit to T16's last Done-when in `tasks.md` (contract page sync ok). It is not part of this report and the Verifier left it untouched |
| Fix 1–5 (cycle 1) | ✅ Done | `bbc8091`, `07c1cab`, `cb8185d`, `8d1527d`, `60ecbaf` |

---

## Gate Check

- **Host build gate**: `cmake --preset default && cmake --build --preset default && ctest --preset default` ➔ 123/123 passed, 0 skipped. That is 120 in cycle 1 plus the 3 new `V4l2SinkTest` cases.
- **ROS gate**: `tools/jazzy/run.sh /bridge/tools/jazzy/test-ros.sh` ➔ 3/3 passed (`bridge_node_rostest`, `camera_bridge_rostest`, `unreadable_map_test`).
- **Flakiness reruns**: in `ctest -R "bridge_node_rostest|camera_bridge_rostest" --repeat until-fail:8`, `camera_bridge_rostest` failed once at `camera_bridge_rostest.cpp:219` (`EXPECT_EQ(2u, sinks_["front"]->Count())`, actual 1). It then passed 25/25 as separate processes and 30/30 in-process (`--gtest_repeat=30`). `bridge_node_rostest` passed 15/15 (`--gtest_repeat=15`). The two tests changed by the fixes (`StuckCameraDoesNotDelayTelemetry`, `DuplicateEvsViewLogsCamMapInvalidAndKeepsTelemetry`) never failed.
- **Lint**: `clang-format --dry-run --Werror` is clean on the 23 touched C++ files outside `android/`. `clang-tidy -p build` reports 0 findings on the 9 host-buildable ones (core src/test, `v4l2_sink.cpp`, `v4l2_sink_test.cpp`).
- **Test count**: 85 `TEST`/`TEST_F` at `1f55d54`, 134 at HEAD. No test was removed and no assertion was weakened. `07c1cab` moved `start` before the publish and added `started`/`Count()` checks, which is stricter.
- **Emulator** (read-only adb, `emulator-5554`): `cmd car_service get-camera-id <TYPE>` prints `CarEvsService is using /dev/video10 for FRONTVIEW.` and the same for /dev/video11..13 with REARVIEW, LEFTVIEW and RIGHTVIEW. That matches the format in AOSP `CarShellCommand.java:3640` and the grep in `tools/emu/test-cam.sh:394`. The Verifier did not run `test-cam.sh` or `test-e2e.sh` (they need reboots and installs). Emulator results below marked "reported" come from the phase-4 worker.

---

## Spec-Anchored Acceptance Criteria

| ID | Spec-defined outcome | Evidence (`file:line` + assertion) | Result |
| -- | -------------------- | ---------------------------------- | ------ |
| CAM-01 | JPEG 1280×720 on `<pos>` reaches the mapped EVS camera in 1280×720 | `core/test/camera_channel_test.cpp:94` YUYV bytes equal and size `kW*kH*2`. `ros/vipee_bridge/test/camera_bridge_rostest.cpp:195-205` `EXPECT_EQ(vector{kFrameBytes}, sinks_["rear"]->Lens())`, other sinks 0. `tools/emu/test-cam.sh:238-241` `check_rate` width×height (reported) | ✅ PASS |
| CAM-02 | ≥ 8 frames/s per camera on a client | `tools/emu/test-cam.sh:268` `check_rate … $BAG_S bag` per device (reported). The client is the evsmanagerd probe, not `CarEvsManager` (see notes) | ✅ PASS (reported) |
| CAM-03 | ≤ 100 ms from receipt to device, measured in the ROS process | `tools/emu/test-cam.sh:273-278` `exit !(max <= 100)` over every STATS p95 (reported). `core/test/camera_channel_test.cpp:223` p95 nearest-rank math. Mutant N10 (rx stamped 0) survives the ROS suite. Only this emulator check would catch it | ✅ PASS (reported) |
| CAM-04 | Each frame at most once, no repeat | `core/test/camera_channel_test.cpp:107` 1 write after 40 ticks. `camera_bridge_rostest.cpp:199-200` one write after 400 ms. `tools/emu/test-cam.sh:164` `last_frame_ms_ago >= 2000` on the HAL fork (reported) | ✅ PASS |
| CAM-05 | ct-speed-slice ≤ 200 ms / ≤ 600 ms with 4 cameras on | `camera_bridge_rostest.cpp:249-257`: `start` is taken before the back-to-back publish, then `EXPECT_LT(… , 500ms)` while `started == 1` and `Count() == 0` (sink still inside the 1.5 s write). This kills R3. `tools/emu/test-e2e.sh:23,29,162` cameras `ok` + `cam_stats` (reported 121 ms / 561 ms) | ✅ PASS |
| CAM-06 | > stale_ms without a valid frame ➔ stop, `CAM <pos> STALE` | `core/test/camera_channel_test.cpp:113` none at 500 ms, STALE at 500 ms + 1 ns. `camera_bridge_rostest.cpp:212-214`. `tools/emu/test-cam.sh:297` (reported) | ✅ PASS |
| CAM-07 | Valid frame in STALE ➔ delivered, `CAM <pos> LIVE`, no reopen | `core/test/camera_channel_test.cpp:122`. `camera_bridge_rostest.cpp:216-219` is **flaky** (Gap 1). `tools/emu/test-cam.sh:306` (reported) | ⚠️ PASS, flaky ROS test |
| CAM-08 | A stopped camera does not interrupt the others or the VHAL | `camera_bridge_rostest.cpp:249-257` (as CAM-05). `tools/emu/test-cam.sh:329,339` right camera unaffected (reported) | ✅ PASS |
| CAM-09 | Other size ➔ drop, no resize, `FRAME_SIZE_MISMATCH` | `core/test/jpeg_yuyv_test.cpp:108` reason and output untouched | ✅ PASS |
| CAM-10 | Non-JPEG ➔ `FORMAT_UNSUPPORTED` | `core/test/jpeg_yuyv_test.cpp:115` `png`, `""`. Kills N2 | ✅ PASS |
| CAM-11 | Decode failure ➔ `DECODE_ERROR` | `core/test/jpeg_yuyv_test.cpp:124,132,139` | ✅ PASS |
| CAM-12 | `CAM <pos> DROP <reason>` once per reason change | `core/test/camera_channel_test.cpp:146` exact line list. Kills N8. `tools/emu/test-cam.sh:331-336` (reported) | ✅ PASS |
| CAM-13 | Dropped frame counts as absent | `core/test/camera_channel_test.cpp:138`. At the ROS layer nothing pins that `tick()` runs while invalid frames keep arriving: N7 survives (Gap 2) | ⚠️ PASS in core; ROS loop not pinned |
| CAM-14 | Device fails ➔ `DEVICE_UNAVAILABLE`, retry every 1 s, process alive | `core/test/camera_channel_test.cpp:167,199` open counts at 1099/1100 ms and a single log line (kills N1). `ros/vipee_bridge/test/v4l2_sink_test.cpp:71` adjusted format refused (kills M11). `:83` short write incomplete (kills M10). `tools/emu/test-cam.sh:357,366` (reported) | ✅ PASS |
| CAM-15 | Others and VHAL keep running while one device is down | `tools/emu/test-cam.sh:361` 3 cameras ≥ 8/s + VHAL (reported) | ✅ PASS (reported) |
| CAM-16 | Topic, view, width, height, stale_ms from the selected map | `ros/vipee_bridge/test/bridge_node_rostest.cpp:255-290` exact values from both real maps. Kills N5 | ✅ PASS |
| CAM-17 | Invalid camera section ➔ no camera opens, `CAM MAP_INVALID`, VHAL as before | `bridge_node_rostest.cpp:310-335`: `EXPECT_EQ(0, sinks_made)` after 200 ms of spinning. The only camera log line is `CAM MAP_INVALID DUPLICATE_VIEW`, and telemetry is delivered with status 0. Kills R1 and N6. `core/test/vehicle_map_test.cpp:177` size ≤ 0 (kills N3) | ✅ PASS |
| CAM-18 | No `cameras` ➔ telemetry only, no error | `bridge_node_rostest.cpp:292` | ✅ PASS |
| CAM-19 | After install, FRONTVIEW/REARVIEW/LEFTVIEW/RIGHTVIEW available to `CarEvsManager` | `tools/emu/test-cam.sh:392-396` `get-camera-id $t \| grep -q "/dev/video$i for $t"` for i = 10..13. The format was checked against AOSP `CarShellCommand.java:3640` and live emulator output (Verifier, read-only) | ✅ PASS |
| CAM-20 | Remove ➔ previous HAL and camera config | `tools/emu/test-cam.sh:380-385` all 4 `CAMERA cam_$p` listed, `init.svc.vendor.evs-v4l2 != running` | ✅ PASS (script not run by Verifier) |
| CAM-21 | Enforcing, 0 `avc: denied` | `tools/emu/test-cam.sh:79` `avc_check` in every step (reported) | ✅ PASS (reported) |
| CAM-22 | Bag loop keeps delivering | `core/test/camera_channel_test.cpp:212`. 60 s run over a 24 s bag (reported) | ✅ PASS |
| CAM-23 | Only the newest of two pending frames, silently | `core/test/latest_slot_test.cpp:16`. `camera_bridge_rostest.cpp:222` burst ➔ last luma | ✅ PASS |
| CAM-24 | No client ➔ accept without accumulating memory | `core/test/latest_slot_test.cpp:52` | ✅ PASS |

**Status**: 24/24 covered with evidence. CAM-07's ROS test is flaky (Gap 1), and the ROS camera loop does not pin CAM-13 (Gap 2).

### Spec-precision notes (carried over, unchanged)

- CAM-12: the same reason is logged again after a valid frame in between. The test pins this reading.
- CAM-02/CAM-19 say `CarEvsManager`. Automation goes through the evsmanagerd probe and `cmd car_service get-camera-id`. Design.md sends the Java client to the manual AVM success criterion.

---

## Discrimination Sensor

The sensor ran in a temporary `git worktree` (`scratchpad/wt`, detached at `60ecbaf`). Host mutants were built in `wt/build`. ROS mutants were built in the container under `/ws/ros_test/build_bridge_mut` and run with `ctest`. Afterwards the worktree was removed, `git worktree prune` was run, and `build_bridge_mut` was deleted. The real tree's `git status --porcelain` matched the pre-sensor baseline exactly (only ` M .specs/features/cam-evs-slice/tasks.md`).

| # | File:line | Mutation | Killed? |
| - | --------- | -------- | ------- |
| R1 | `ros/vipee_bridge/src/bridge_node.cpp:128` | CameraBridge built despite `CAM MAP_INVALID` | ✅ `DuplicateEvsViewLogsCamMapInvalidAndKeepsTelemetry` |
| R3 | `ros/vipee_bridge/src/camera_bridge.cpp:77` | decode+write in the subscription callback | ✅ `StuckCameraDoesNotDelayTelemetry` |
| M10 | `ros/vipee_bridge/src/v4l2_sink.cpp:19` | short write accepted (`written >= 0`) | ✅ `OnlyAFullWriteIsComplete` |
| M11 | `ros/vipee_bridge/src/v4l2_sink.cpp:14-15` | driver-enlarged width/height accepted (`>=`) | ✅ `DriverAdjustedFormatIsRefused` |
| N1 | `core/src/camera_channel.cpp:101` | `DEVICE_UNAVAILABLE` logged on every failed open | ✅ `WriteFailureMarksDeviceUnavailableAndRetriesEachSecond` |
| N2 | `core/src/jpeg_yuyv.cpp:40` | format check disabled | ✅ `FormatWithoutJpegIsUnsupported` + 3 channel tests |
| N3 | `core/src/vehicle_map.cpp:36` | width 0 accepted (`< 0`) | ✅ `NonPositiveSizeIsRejected` |
| N4 | `core/src/camera_channel.cpp:90` | `takeStats` does not reset the counters | ✅ `StatsCountFramesDropsAndP95Latency` |
| N5 | `ros/vipee_bridge/src/bridge_node.cpp:100` | `device` not read from the map | ✅ `BothRealMapsLoadFourCamerasWithTheFileValues` + 5 others |
| N6 | `ros/vipee_bridge/src/bridge_node.cpp:125-126` | no `CAM MAP_INVALID` log | ✅ `DuplicateEvsViewLogsCamMapInvalidAndKeepsTelemetry` |
| N7 | `ros/vipee_bridge/src/camera_bridge.cpp:44` | `tick()` skipped when the iteration handled a frame | ❌ Survived (Gap 2) |
| N8 | `core/src/camera_channel.cpp:53` | a valid frame does not reset the last drop reason | ✅ `DropIsLoggedOncePerReasonChange` |
| N9 | `ros/vipee_bridge/src/camera_bridge.cpp:75` | subscription `keep_last(1)` ➔ `keep_last(10)` | ⚪ Survived, equivalent: `LatestSlot` still keeps one frame, and the DDS history is bounded |
| N10 | `ros/vipee_bridge/src/camera_bridge.cpp:77` | receipt stamp `rx_ns = 0` | ⚪ Survived the host and ROS suites. `test-cam.sh:273-278` (p95 ≤ 100 ms) catches it by construction. Not counted as a gap because CAM-03 is measured on the emulator by spec |

**Sensor depth**: expanded (data-integrity path).
**Result**: 11/14 killed. Of the 3 survivors, N9 is equivalent, N10 is covered only by the emulator suite, and N7 is a real gap. All 4 cycle-1 survivors are killed. FAIL ❌ on N7.

Why N7 matters: `onFrame` returns early on a dropped frame, so only `tick()` can declare STALE. `slot.take(50 ms)` returns at once whenever a frame is waiting. A camera fed invalid frames more often than every 50 ms therefore never reaches the `tick()` that N7 removes, and it never goes STALE. That would break CAM-13 at the ROS layer. The emulator drop test publishes at about 10 Hz (100 ms gaps), so it cannot see this either.

The forked HAL (`android/evs_hal`) is still not mutation-tested (AOSP code, no host seam). It is covered only by `test-cam.sh stall`/`platform` (reported).

---

## API Correctness (context7 / source)

- The cycle-1 findings for TurboJPEG 3, rclcpp Jazzy and V4L2 still hold. The only production change is `cb8185d`, which moves the existing checks into `v4l2FormatAccepted`/`v4l2WriteComplete` without changing their logic.
- `cmd car_service get-camera-id`: AOSP `packages/services/Car/service/src/com/android/car/CarShellCommand.java:3640` prints `CarEvsService is using %s for %s.` The `test-cam.sh:394` grep matches that format, confirmed on the live emulator.

---

## Code Quality

| Principle | Status |
| --------- | ------ |
| Minimum code / surgical | ✅ Fixes are test-only, except the two pure functions in `v4l2_sink.cpp:12-20`, which have Doxygen in the header |
| Matches patterns | ✅ Counting factory and `started` counter follow the existing fakes. No mocks of rclcpp or the HAL |
| Spec-anchored outcome check | ✅ The fix tests assert outcomes (0 sinks made, < 500 ms while the write is pending, exact device per type, all 4 MP4 cameras listed), not implementation details |
| Per-layer coverage | ⚠️ ROS camera loop: CAM-13 under a fast invalid stream is not pinned (N7) |
| Test reliability (AGENTS.md §3, suite 100% green) | ❌ `camera_bridge_rostest.cpp:216-219` race (Gap 1) |
| Guidelines: `AGENTS.md` §3, §3.1, §4 | ✅ Lint clean, Doxygen present |

---

## Fix Plans

### Fix 1 (Minor): `SilencedTopicGoesStaleWithoutNewFramesThenLive` is flaky

- **Root cause**: `CameraChannel::onFrame` logs `LIVE` (`core/src/camera_channel.cpp:55`) before `sink_.write()` (`:60`), on the camera thread. The test waits for `LIVE == 2` (`camera_bridge_rostest.cpp:217-218`) and then reads `Count()` at once (`:219`), so the read can land before the write finishes. Observed once in about 85 runs.
- **Fix task**: wait on the outcome itself: `SpinUntil([&]{ return camera_logger_.Count("CAM front LIVE") == 2 && sinks_["front"]->Count() == 2; })`, then `SpinFor(200)` and `EXPECT_EQ(2u, Count())` to keep the "no extra write" check. Production code stays as is.

### Fix 2 (Minor): ROS camera loop does not pin CAM-13 under fast invalid input (N7)

- **Fix task**: in `camera_bridge_rostest.cpp`, publish non-JPEG (or wrong-size) frames to one camera every 20 ms for longer than `stale_ms` + 200 ms, after one valid frame. Assert `CAM <pos> STALE` appears within `stale_ms` + 200 ms and the sink count stays 1. The test must kill N7 (`camera_bridge.cpp:44` → `if (!item) channel.tick(now);`).

---

## Requirement Traceability Update

| Requirement | New Status |
| ----------- | ---------- |
| CAM-01..06, 08..12, 14..24 | ✅ Verified |
| CAM-07 | ⚠️ Verified; ROS test flaky (Fix 1) |
| CAM-13 | ⚠️ Verified in core; ROS loop needs Fix 2 |

---

## Summary

**Overall**: ❌ Not Ready (2 Minor, test-only gaps)

**Spec-anchored check**: 24/24 ACs have evidence that matches the spec outcome. All 5 cycle-1 gaps are closed. CAM-07 is flaky and CAM-13 is weak at the ROS layer.
**Sensor**: 11/14 killed (R1, R3, M10, M11 now killed). Survivors: N7 (real gap), N9 (equivalent), N10 (emulator-only coverage).
**Gate**: host 123/123, ROS 3/3 (1 flaky failure in reruns), lint clean.

**Next steps**: Fix 1 and Fix 2 (ROS tests only), then re-dispatch the Verifier (iteration 3 of 3).
