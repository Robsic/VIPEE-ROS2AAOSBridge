# cam-evs-slice Validation

**Date**: 2026-11-02
**Spec**: `.specs/features/cam-evs-slice/spec.md`
**Diff range**: `1f55d54..31f3262` (25 commits; cycle-3 fixes `e6e0648`, `31f3262`)
**Verifier**: independent sub-agent, cycle 3 (author ≠ verifier; did not write the code or the earlier reports)
**Verdict**: FAIL

Both cycle-2 gaps are closed. `SilencedTopicGoesStaleWithoutNewFramesThenLive` now waits on the write itself and passed 45/45 reruns. The new `InvalidFramesFasterThanTheTickStillGoStale` kills N7. The verdict stays FAIL because two new behavior-level mutants survive every automated suite:

1. **V5 (Major, test gap, CAM-09)**: `core/src/jpeg_yuyv.cpp:52` `!= height` ➔ `< height`. A JPEG taller than the map (e.g. 1280×800, or the SynthCam 1280×966) is accepted and decoded into planes sized for 720 rows. A probe test in the scratch worktree segfaulted under this mutant. Every size-mismatch test uses a JPEG smaller in both dimensions (`core/test/jpeg_yuyv_test.cpp:110`, `core/test/camera_channel_test.cpp:149`), so nothing pins "larger than the map". The same holds for the width check at `:51`.
2. **W1 (Minor, test gap, CAM-06)**: `ros/vipee_bridge/src/camera_bridge.cpp:16` tick period 50 ms ➔ 1000 ms. The idle STALE log moves from about 550 ms to up to about 1.5 s after the last frame. The spec's Independent Test requires `STALE` within 600 ms. Neither `camera_bridge_rostest.cpp:212-213` (5 s default timeout) nor `tools/emu/test-cam.sh:297` (about 4.6 s) bounds it.

No production defect was found. The production code is correct today; the tests do not pin these two outcomes. This is iteration 3 of 3, so the gaps escalate to the user.

---

## Cycle History

| Cycle | Commit | Verdict | Gaps / survivors |
| ----- | ------ | ------- | ---------------- |
| 1 | `1bf5f6e` | FAIL | CAM-17 not asserted (R1). CAM-05/08 ROS test not discriminating (R3). V4l2Sink success path untested (M10, M11). CAM-19 not asserted. CAM-20 partial |
| 2 | `91b0d6f` | FAIL | Cycle-1 gaps all closed. New: flaky race at `camera_bridge_rostest.cpp:219`; survivor N7 (`camera_bridge.cpp:44`) |
| 3 | this report | FAIL | Cycle-2 gaps closed (`e6e0648`, `31f3262`). New survivors: V5 (`jpeg_yuyv.cpp:51-52`, CAM-09), W1 (`camera_bridge.cpp:16`, CAM-06 timing) |

Fix→re-verify iteration 3 of 3: escalate to the user.

---

## Task Completion

| Task | Status | Notes |
| ---- | ------ | ----- |
| T1–T16 | ✅ Done | All `[x]`. The working tree has a pre-existing unstaged edit to T16's last Done-when in `tasks.md` (contract page sync ok). It is not part of this report and the Verifier left it untouched |
| Fix 1–5 (cycle 1) | ✅ Done | `bbc8091`, `07c1cab`, `cb8185d`, `8d1527d`, `60ecbaf` |
| Fix 1–2 (cycle 2) | ✅ Done | `e6e0648` (CAM-07 race), `31f3262` (CAM-13 at the ROS layer, kills N7) |

---

## Gate Check

- **Host build gate**: `cmake --preset default && cmake --build --preset default && ctest --preset default` ➔ 123/123 passed, 0 skipped.
- **ROS gate**: `tools/jazzy/run.sh /bridge/tools/jazzy/test-ros.sh` ➔ 3/3 passed (`bridge_node_rostest`, `camera_bridge_rostest`, `unreadable_map_test`).
- **Flakiness reruns**: `camera_bridge_rostest` ran 46 times as separate `ctest` processes (1 warm-up plus 45 counted): 46/46 passed, 0 failures.
- **Lint**: `clang-format --dry-run --Werror` is clean on the 23 touched C++ files outside `android/`. `clang-tidy -p build` exits 0 with no findings on the host-buildable `.cpp` files (core src/test, `v4l2_sink.cpp`, `v4l2_sink_test.cpp`).
- **Test integrity**: `git diff 60ecbaf..HEAD` touches one test file, `camera_bridge_rostest.cpp` (+40/−2). The 2 removed lines are the old `LIVE`-only wait, replaced by a stricter wait (LIVE and write), then 200 ms, then the count. One test was added and none was removed or weakened.
- **Emulator**: not used this cycle. The cycle-3 changes are ROS-test only and cycle 2 already did the read-only `get-camera-id` check. Emulator results marked "reported" come from the phase-4 worker.

---

## Spec-Anchored Acceptance Criteria

Unless noted, test files other than `camera_bridge_rostest.cpp` are unchanged since cycle 2 (`git diff 60ecbaf..HEAD`). Their line citations carry over, and this cycle's sensor re-confirmed them by killing the mutants they cover.

| ID | Spec-defined outcome | Evidence (`file:line` + assertion) | Result |
| -- | -------------------- | ---------------------------------- | ------ |
| CAM-01 | JPEG 1280×720 on `<pos>` reaches the mapped EVS camera in 1280×720 | `core/test/camera_channel_test.cpp:94` YUYV bytes equal and size `kW*kH*2`. `ros/vipee_bridge/test/camera_bridge_rostest.cpp:195-206` `EXPECT_EQ(vector{kFrameBytes}, sinks_["rear"]->Lens())`, other sinks 0. `tools/emu/test-cam.sh:238-241` `check_rate` width×height (reported) | ✅ PASS |
| CAM-02 | ≥ 8 frames/s per camera on a client | `tools/emu/test-cam.sh:268` `check_rate … $BAG_S bag` per device (reported). The client is the evsmanagerd probe, not `CarEvsManager` (see notes) | ✅ PASS (reported) |
| CAM-03 | ≤ 100 ms from receipt to device, measured in the ROS process | `tools/emu/test-cam.sh:273-278` `exit !(max <= 100)` over every STATS p95 (reported). `core/test/camera_channel_test.cpp:223` p95 nearest-rank math. Mutant N10 (rx stamped 0) survives the ROS suite. Only this emulator check would catch it | ✅ PASS (reported) |
| CAM-04 | Each frame at most once, no repeat | `core/test/camera_channel_test.cpp:107` 1 write after 40 ticks. `camera_bridge_rostest.cpp:199-200` one write after 400 ms. `tools/emu/test-cam.sh:164` `last_frame_ms_ago >= 2000` on the HAL fork (reported) | ✅ PASS |
| CAM-05 | ct-speed-slice ≤ 200 ms / ≤ 600 ms with 4 cameras on | `camera_bridge_rostest.cpp:274-294`: `start` is taken before the back-to-back publish, then `EXPECT_LT(… , 500ms)` (`:292`) while `Count() == 0` (`:293`, sink still inside the 1.5 s write). Kills R3 (re-run this cycle). `tools/emu/test-e2e.sh:23,29,162` cameras `ok` + `cam_stats` (reported 121 ms / 561 ms) | ✅ PASS |
| CAM-06 | > stale_ms without a valid frame ➔ stop, `CAM <pos> STALE` | `core/test/camera_channel_test.cpp:113` none at 500 ms, STALE at 500 ms + 1 ns. `camera_bridge_rostest.cpp:212-214` (no time bound on the STALE log: W1 survives, Gap 2). `tools/emu/test-cam.sh:297` waits up to ~4.6 s after the pause (reported) | ⚠️ PASS; STALE log latency not pinned |
| CAM-07 | Valid frame in STALE ➔ delivered, `CAM <pos> LIVE`, no reopen | `core/test/camera_channel_test.cpp:122`. `camera_bridge_rostest.cpp:216-223`: waits for `LIVE == 2` **and** `Count() == 2`, spins 200 ms, then `EXPECT_EQ(2u, Count())` (no rewrite). 45/45 reruns green. `tools/emu/test-cam.sh:306` (reported) | ✅ PASS |
| CAM-08 | A stopped camera does not interrupt the others or the VHAL | `camera_bridge_rostest.cpp:274-294` (as CAM-05). `tools/emu/test-cam.sh:329,339` right camera unaffected (reported) | ✅ PASS |
| CAM-09 | Other size ➔ drop, no resize, `FRAME_SIZE_MISMATCH` | `core/test/jpeg_yuyv_test.cpp:108-113` reason and output untouched, but only for a JPEG **smaller** in both dimensions (480×480). A larger or same-width/taller JPEG is never tested: V5 survives (Gap 1) | ❌ GAP |
| CAM-10 | Non-JPEG ➔ `FORMAT_UNSUPPORTED` | `core/test/jpeg_yuyv_test.cpp:115` `png`, `""`. Kills N2 | ✅ PASS |
| CAM-11 | Decode failure ➔ `DECODE_ERROR` | `core/test/jpeg_yuyv_test.cpp:124,132,139` | ✅ PASS |
| CAM-12 | `CAM <pos> DROP <reason>` once per reason change | `core/test/camera_channel_test.cpp:146` exact line list. Kills N8. `tools/emu/test-cam.sh:331-336` (reported) | ✅ PASS |
| CAM-13 | Dropped frame counts as absent | `core/test/camera_channel_test.cpp:138`. ROS layer: `camera_bridge_rostest.cpp:226-256`, one valid frame then `png` every 20 ms; `EXPECT_TRUE(… Count("CAM right STALE") == 1, kStaleMs + 200)` (`:244`) and `EXPECT_EQ(1u, Count())` (`:255`). Kills N7 | ✅ PASS |
| CAM-14 | Device fails ➔ `DEVICE_UNAVAILABLE`, retry every 1 s, process alive | `core/test/camera_channel_test.cpp:167,199` open counts at 1099/1100 ms and a single log line (kills N1). `ros/vipee_bridge/test/v4l2_sink_test.cpp:71` adjusted format refused (kills M11). `:83` short write incomplete (kills M10). `tools/emu/test-cam.sh:357,366` (reported) | ✅ PASS |
| CAM-15 | Others and VHAL keep running while one device is down | `tools/emu/test-cam.sh:361` 3 cameras ≥ 8/s + VHAL (reported) | ✅ PASS (reported) |
| CAM-16 | Topic, view, width, height, stale_ms from the selected map | `ros/vipee_bridge/test/bridge_node_rostest.cpp:255-290` exact values from both real maps. Kills N5 | ✅ PASS |
| CAM-17 | Invalid camera section ➔ no camera opens, `CAM MAP_INVALID`, VHAL as before | `bridge_node_rostest.cpp:310-335`: `EXPECT_EQ(0, sinks_made)` after 200 ms of spinning. The only camera log line is `CAM MAP_INVALID DUPLICATE_VIEW`, and telemetry is delivered with status 0. Kills R1 and N6. `core/test/vehicle_map_test.cpp:177` size ≤ 0 (kills N3) | ✅ PASS |
| CAM-18 | No `cameras` ➔ telemetry only, no error | `bridge_node_rostest.cpp:292` | ✅ PASS |
| CAM-19 | After install, FRONTVIEW/REARVIEW/LEFTVIEW/RIGHTVIEW available to `CarEvsManager` | `tools/emu/test-cam.sh:392-396` `get-camera-id $t \| grep -q "/dev/video$i for $t"` for i = 10..13. The format was checked against AOSP `CarShellCommand.java:3640` and live emulator output (Verifier, read-only) | ✅ PASS |
| CAM-20 | Remove ➔ previous HAL and camera config | `tools/emu/test-cam.sh:380-385` all 4 `CAMERA cam_$p` listed, `init.svc.vendor.evs-v4l2 != running` | ✅ PASS (script not run by Verifier) |
| CAM-21 | Enforcing, 0 `avc: denied` | `tools/emu/test-cam.sh:79` `avc_check` in every step (reported) | ✅ PASS (reported) |
| CAM-22 | Bag loop keeps delivering | `core/test/camera_channel_test.cpp:212`. 60 s run over a 24 s bag (reported) | ✅ PASS |
| CAM-23 | Only the newest of two pending frames, silently | `core/test/latest_slot_test.cpp:16`. `camera_bridge_rostest.cpp:258-272` burst ➔ `EXPECT_NEAR(200, luma.back(), 3)` (`:271`) | ✅ PASS |
| CAM-24 | No client ➔ accept without accumulating memory | `core/test/latest_slot_test.cpp:52` | ✅ PASS |

**Status**: 24/24 covered with evidence. CAM-09 lacks a test for a frame larger than the map (Gap 1). CAM-06's STALE log latency is not bounded by any test (Gap 2).

### Spec-precision notes (carried over, unchanged)

- CAM-12: the same reason is logged again after a valid frame in between. The test pins this reading.
- CAM-02/CAM-19 say `CarEvsManager`. Automation goes through the evsmanagerd probe and `cmd car_service get-camera-id`. Design.md sends the Java client to the manual AVM success criterion.
- CAM-06: the AC gives no deadline for the `STALE` log. The 600 ms bound comes from the story's Independent Test, and Gap 2 uses it.

---

## Discrimination Sensor

The sensor ran in a temporary `git worktree` (`scratchpad/wt`, detached at `31f3262`). Host mutants were built in `wt/build` with the default preset. ROS mutants were built in the Jazzy container from the worktree into `/ws/ros_test/build_bridge_mut` (`ROS_DOMAIN_ID=78`), and all 3 ROS tests ran under `ctest`. The unmutated baseline passed in both scratch builds. Afterwards the worktree was removed, `git worktree prune` was run, and `build_bridge_mut` was deleted. The real tree's `git status --porcelain` matched the pre-sensor baseline exactly (only ` M .specs/features/cam-evs-slice/tasks.md`).

| # | File:line | Mutation | Killed? |
| - | --------- | -------- | ------- |
| N7 | `ros/vipee_bridge/src/camera_bridge.cpp:44` | `if (!item) channel.tick(now);` | ✅ `InvalidFramesFasterThanTheTickStillGoStale` (`camera_bridge_rostest.cpp:244`) |
| R1 | `ros/vipee_bridge/src/bridge_node.cpp:129` | CameraBridge built despite `CAM MAP_INVALID` | ✅ `DuplicateEvsViewLogsCamMapInvalidAndKeepsTelemetry` (`bridge_node_rostest.cpp:333-334`) |
| R3 | `ros/vipee_bridge/src/camera_bridge.cpp:77` | decode+write in the subscription callback | ✅ `StuckCameraDoesNotDelayTelemetry` (`camera_bridge_rostest.cpp:290`) |
| M10 | `ros/vipee_bridge/src/v4l2_sink.cpp:19` | short write accepted (`written >= 0`) | ✅ `V4l2SinkTest.OnlyAFullWriteIsComplete` |
| M11 | `ros/vipee_bridge/src/v4l2_sink.cpp:14-15` | driver-enlarged width/height accepted (`>=`) | ✅ `V4l2SinkTest` (1 failure) |
| V1 | `core/src/camera_channel.cpp:74` | stale limit doubled | ✅ 3 `CameraChannelTest` (`StaleOnlyAfterMoreThanStaleMs`, …) |
| V2 | `core/src/camera_channel.cpp:54` | valid frame does not renew `last_valid_ns_` | ✅ `StaleOnlyAfterMoreThanStaleMs` |
| V3 | `core/src/camera_channel.cpp:55` | `LIVE` never logged | ✅ 5 `CameraChannelTest` |
| V4 | `core/src/camera_channel.cpp:10` | reopen period 1 s ➔ 500 ms | ✅ `WriteFailureMarksDeviceUnavailableAndRetriesEachSecond`, `DeviceThatDoesNotOpenIsUnavailableAndRetried` |
| V5 | `core/src/jpeg_yuyv.cpp:52` | height check `!=` ➔ `<` (taller JPEG accepted) | ❌ Survived (Gap 1) |
| V6 | `core/src/vehicle_map.cpp:37` | `stale_ms <= 0` check removed | ✅ `CameraMap.NonPositiveStaleMsIsRejected` |
| V7 | `core/include/vipee/latest_slot.h:37` | `put` keeps the oldest value | ✅ `LatestSlot.TwoPutsBeforeTakeDeliverOnlyTheNewest`, `PutsWithoutTakeKeepOneValue` |
| V8 | `core/src/camera_channel.cpp:64` | delivered frames not counted in STATS | ✅ `StatsCountFramesDropsAndP95Latency` |
| W1 | `ros/vipee_bridge/src/camera_bridge.cpp:16` | tick period 50 ms ➔ 1000 ms | ❌ Survived (Gap 2) |
| W2 | `ros/vipee_bridge/src/bridge_node.cpp:107` | `stale_ms` read from the map then doubled | ✅ `BothRealMapsLoadFourCamerasWithTheFileValues` (`bridge_node_rostest.cpp:287`), `InvalidFramesFasterThanTheTickStillGoStale` |
| W3 | `ros/vipee_bridge/src/camera_bridge.cpp:38` | camera loop skips non-JPEG items before the channel (no `DROP FORMAT_UNSUPPORTED`) | ⚪ Survived the host and ROS suites. `tools/emu/test-cam.sh:331-336` (rear PNG ➔ exactly one `CAM rear DROP FORMAT_UNSUPPORTED`) catches it by construction. Emulator-only coverage, like N10 in cycle 2. Not a gap |

**Sensor depth**: expanded (data-integrity path). The cycle-2 mutants N9 (equivalent) and N10 (emulator-only) were not re-run; their code is unchanged.
**Result**: 13/16 killed. No survivor is equivalent. W3 is covered by the emulator suite. V5 and W1 are real gaps. FAIL ❌.

**V5 is not equivalent.** In the scratch worktree, a probe `decode("jpeg", makeJpeg(1280, 800), …)` expecting `kFrameSizeMismatch` passed on the unmutated code and **segfaulted** under V5. The 720-row planes are sized from the map, and the decoder writes the JPEG's 800 rows into them. The probe was discarded with the worktree.

**W1 is not equivalent.** On an idle camera `slot.take(kTickPeriod)` blocks for the whole period, so `tick()` declares STALE up to one period after `stale_ms`. With 50 ms that is ≤ 550 ms, inside the Independent Test's 600 ms. With 1000 ms it is up to about 1.5 s. N7's new test does not catch it, because the 20 ms invalid stream keeps the loop awake.

The forked HAL (`android/evs_hal`) is still not mutation-tested (AOSP code, no host seam). It is covered only by `test-cam.sh stall`/`platform` (reported).

---

## API Correctness (context7 / source)

No production code changed since cycle 2. The cycle-1 and cycle-2 findings for TurboJPEG 3, rclcpp Jazzy, V4L2 and `cmd car_service get-camera-id` still apply.

---

## Code Quality

| Principle | Status |
| --------- | ------ |
| Minimum code / surgical | ✅ Cycle-3 fixes are test-only. They add one test and fix one wait |
| Matches patterns | ✅ Same `SpinUntil`/fake-sink style. No mocks of rclcpp or the HAL |
| Spec-anchored outcome check | ✅ The new test asserts STALE within `stale_ms + 200` ms and no extra write. The CAM-07 fix asserts the write count, not only the log |
| Per-layer coverage | ⚠️ Core: CAM-09 does not cover a frame larger than the map (V5). ROS: the idle STALE deadline is not bounded (W1) |
| Test reliability (AGENTS.md §3) | ✅ 46/46 `camera_bridge_rostest` runs green |
| Guidelines: `AGENTS.md` §3, §3.1, §4 | ✅ Lint clean |

---

## Fix Plans

### Fix 1 (Major): CAM-09 does not reject frames larger than the map (V5)

- **Root cause**: every `FRAME_SIZE_MISMATCH` test uses a JPEG smaller in both dimensions (`core/test/jpeg_yuyv_test.cpp:110` 480×480, `core/test/camera_channel_test.cpp:149` 32×32). A `<` instead of `!=` at `core/src/jpeg_yuyv.cpp:51-52` is invisible, and it would overflow the decode planes on a taller frame.
- **Fix task**: in `core/test/jpeg_yuyv_test.cpp`, add `OtherSizeLargerThanTheMapIsFrameSizeMismatch`. `makeJpeg(kW, 800, TJSAMP_420)` and `makeJpeg(1344, kH, TJSAMP_420)` must each return `kFrameSizeMismatch` and leave `out` untouched. Production code stays as is. The test must kill V5 and its width twin.

### Fix 2 (Minor): CAM-06 STALE deadline not bounded on an idle camera (W1)

- **Root cause**: `camera_bridge_rostest.cpp:212-213` waits up to the 5 s default for `CAM front STALE`. The emulator stale step allows about 4.6 s.
- **Fix task**: in `SilencedTopicGoesStaleWithoutNewFramesThenLive`, take `t0` when `Count() == 1` is observed, then `ASSERT_TRUE(SpinUntil(… STALE == 1, 600))` and check that the elapsed time is ≥ 500 ms − 50 ms of slack. The test must kill W1 (`camera_bridge.cpp:16` ➔ 1000 ms). Optionally, tighten `tools/emu/test-cam.sh` to look for STALE in a logcat taken right after the 0.6 s sleep.

---

## Requirement Traceability Update

| Requirement | New Status |
| ----------- | ---------- |
| CAM-01..05, 07, 08, 10..24 | ✅ Verified |
| CAM-06 | ⚠️ Verified; STALE deadline not pinned (Fix 2) |
| CAM-09 | ❌ Needs fix: larger-than-map frames untested (Fix 1) |

---

## Summary

**Overall**: ❌ Not Ready (1 Major and 1 Minor test-only gap; no production defect)

**Spec-anchored check**: 24/24 ACs have evidence. Both cycle-2 gaps are closed. CAM-09 is not pinned for larger frames and CAM-06 is not pinned on timing.
**Sensor**: 13/16 killed. N7, R1, R3, M10, M11 are killed. Survivors: V5 (real), W1 (real), W3 (emulator-only coverage).
**Gate**: host 123/123, ROS 3/3, 46/46 reruns, lint clean.

**Next steps**: the fix→re-verify loop has used 3 of 3 iterations, so this goes to the user. Fix 1 and Fix 2 are test-only and take a few lines each.
