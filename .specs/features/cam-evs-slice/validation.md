# cam-evs-slice Validation

**Date**: 2026-11-02
**Spec**: `.specs/features/cam-evs-slice/spec.md`
**Diff range**: `1f55d54..06608ed` (28 commits; cycle-4 fixes `fc5aac2`, `06608ed`)
**Verifier**: independent sub-agent, cycle 4 (extra cycle authorized by the user; author ≠ verifier; did not write the code or the earlier reports)
**Verdict**: FAIL

Both cycle-3 gaps are closed: `LargerInOneDimensionIsFrameSizeMismatch` kills V5 in both dimensions, and the 600 ms bound in `SilencedTopicGoesStaleWithoutNewFramesThenLive` kills W1 and passed 41/41 reruns. All gates are green. The verdict is FAIL because two of the seven new mutants survive:

1. **X5 (Major, test gap, CAM-11 / design.md:88)**: removing the subsampling guard at `core/src/jpeg_yuyv.cpp:56` survives every suite. The only test (`core/test/jpeg_yuyv_test.cpp:148`, 4:4:4) is also refused by TurboJPEG itself, so it never reaches the guard. A scratch probe showed that, without the guard, a grayscale (`TJSAMP_GRAY`) or 4:4:0 1280×720 JPEG returns `kNone` and is packed into a YUYV frame with wrong chroma. That is a plausible-but-wrong frame injected into EVS, which AGENTS.md §3 forbids.
2. **X3 (Minor, test gap, CAM-03 STATS)**: `core/src/camera_channel.cpp:84` nearest-rank `(n*95+99)/100` ➔ `(n*95)/100` survives. The only p95 test uses n = 20, where ceil and floor agree. Under the mutant, n = 21 reports the 19th value instead of the 20th, and n = 1 indexes `rank - 1 = SIZE_MAX` (out of bounds) on a STATS window with a single frame.

No production defect was found. Production code is unchanged since cycle 2 and is correct; the tests do not pin these two outcomes.

---

## Cycle History

| Cycle | Commit | Verdict | Gaps / survivors |
| ----- | ------ | ------- | ---------------- |
| 1 | `1bf5f6e` | FAIL | CAM-17 not asserted (R1). CAM-05/08 ROS test not discriminating (R3). V4l2Sink success path untested (M10, M11). CAM-19 not asserted. CAM-20 partial |
| 2 | `91b0d6f` | FAIL | Cycle-1 gaps all closed. New: flaky race at `camera_bridge_rostest.cpp:219`; survivor N7 (`camera_bridge.cpp:44`) |
| 3 | `3958768` | FAIL | Cycle-2 gaps closed (`e6e0648`, `31f3262`). Survivors V5 (`jpeg_yuyv.cpp:51-52`, CAM-09), W1 (`camera_bridge.cpp:16`, CAM-06 timing) |
| 4 | this report | FAIL | Cycle-3 gaps closed (`fc5aac2`, `06608ed`). New survivors X5 (`jpeg_yuyv.cpp:56`, CAM-11) and X3 (`camera_channel.cpp:84`, CAM-03 STATS) |

This is an extra iteration beyond the 3-iteration bound; the gaps go back to the user.

---

## Task Completion

| Task | Status | Notes |
| ---- | ------ | ----- |
| T1–T16 | ✅ Done | All `[x]`. The pre-existing unstaged edit to `tasks.md` (T16 Done-when) is not part of this report and was left untouched |
| Fixes cycles 1–3 | ✅ Done | `bbc8091`, `07c1cab`, `cb8185d`, `8d1527d`, `60ecbaf`, `e6e0648`, `31f3262`, `fc5aac2`, `06608ed` |

---

## Gate Check

- **Host build gate**: `cmake --preset default && cmake --build --preset default && ctest --preset default` ➔ 124/124 passed, 0 skipped (123 in cycle 3, +1 `LargerInOneDimensionIsFrameSizeMismatch`).
- **ROS gate**: `tools/jazzy/run.sh /bridge/tools/jazzy/test-ros.sh` ➔ 3/3 passed (`bridge_node_rostest`, `camera_bridge_rostest` 4.60 s, `unreadable_map_test`).
- **Flakiness reruns**: `camera_bridge_rostest` ran 41 more times as separate `ctest` processes (`ROS_DOMAIN_ID=77`), partly while host mutant builds loaded the CPU: 41/41 passed, 0 failures. The 600 ms / 450 ms window held every time.
- **Lint**: `clang-format --dry-run --Werror` clean on both touched test files. `clang-tidy -p build core/test/jpeg_yuyv_test.cpp` exits 0, no findings (`camera_bridge_rostest.cpp` is not in the host compile database).
- **Test integrity**: `git diff 31f3262..HEAD` touches only the two test files (+14/−2). The 2 removed lines are the unbounded STALE wait, replaced by the bounded one. No test removed or weakened.
- **Emulator**: not used. Nothing Android-side changed since cycle 2's read-only check. Results marked "reported" come from the phase-4 worker.

---

## Spec-Anchored Acceptance Criteria

Since cycle 3 (`31f3262`) only two test files changed: `core/test/jpeg_yuyv_test.cpp` (+9 lines at :108, so later lines in that file shift by 9) and `ros/vipee_bridge/test/camera_bridge_rostest.cpp` (+3 lines at :212, so later lines shift by 3). The citations below use the current line numbers. No production file changed (`git diff --stat 31f3262..HEAD`).

| ID | Spec-defined outcome | Evidence (`file:line` + assertion) | Result |
| -- | -------------------- | ---------------------------------- | ------ |
| CAM-01 | JPEG 1280×720 on `<pos>` reaches the mapped EVS camera in 1280×720 | `core/test/camera_channel_test.cpp:94` YUYV bytes equal and size `kW*kH*2`. `ros/vipee_bridge/test/camera_bridge_rostest.cpp:195-206` `EXPECT_EQ(vector{kFrameBytes}, sinks_["rear"]->Lens())`, other sinks 0. `tools/emu/test-cam.sh:238-241` `check_rate` width×height (reported) | ✅ PASS |
| CAM-02 | ≥ 8 frames/s per camera on a client | `tools/emu/test-cam.sh:268` `check_rate … $BAG_S bag` per device (reported). The client is the evsmanagerd probe, not `CarEvsManager` (see notes) | ✅ PASS (reported) |
| CAM-03 | ≤ 100 ms from receipt to device, measured in the ROS process | `tools/emu/test-cam.sh:273-278` `exit !(max <= 100)` over every STATS p95 (reported). `core/test/camera_channel_test.cpp:223-245` p95 bounds for n = 20, the one count where nearest-rank ceil and floor agree: rounding mutant X3 survives (Gap 2). N10 (rx stamped 0) is caught only by the emulator check | ⚠️ PASS (reported); p95 rank not pinned |
| CAM-04 | Each frame at most once, no repeat | `core/test/camera_channel_test.cpp:107` 1 write after 40 ticks. `camera_bridge_rostest.cpp:199-200` one write after 400 ms. `tools/emu/test-cam.sh:164` `last_frame_ms_ago >= 2000` on the HAL fork (reported) | ✅ PASS |
| CAM-05 | ct-speed-slice ≤ 200 ms / ≤ 600 ms with 4 cameras on | `camera_bridge_rostest.cpp:277-296`: `start` taken before the back-to-back publish, then `EXPECT_LT(Clock::now() - start, 500ms)` (`:295`) while `EXPECT_EQ(0u, sinks_["front"]->Count())` (`:296`, sink still inside the 1.5 s write). Kills R3 (re-run this cycle). `tools/emu/test-e2e.sh:23,29,162` (reported 121 ms / 561 ms) | ✅ PASS |
| CAM-06 | > stale_ms without a valid frame ➔ stop, `CAM <pos> STALE` (Independent Test: within 600 ms) | `core/test/camera_channel_test.cpp:113` none at 500 ms, STALE at 500 ms + 1 ns. `camera_bridge_rostest.cpp:213-217`: `first_write` taken when the write is seen, `ASSERT_TRUE(SpinUntil(… STALE == 1, 600))` (`:214`), `EXPECT_GE(… - first_write, 450ms)` (`:216`), `EXPECT_EQ(1u, Count())` (`:217`). Kills W1 (`fc5aac2`/`06608ed` cycle). 41/41 reruns green | ✅ PASS |
| CAM-07 | Valid frame in STALE ➔ delivered, `CAM <pos> LIVE`, no reopen | `core/test/camera_channel_test.cpp:122`. `camera_bridge_rostest.cpp:219-226`: waits for `LIVE == 2` and `Count() == 2`, spins 200 ms, then `EXPECT_EQ(2u, Count())` (`:226`). `tools/emu/test-cam.sh:306` (reported) | ✅ PASS |
| CAM-08 | A stopped camera does not interrupt the others or the VHAL | `camera_bridge_rostest.cpp:277-296` (as CAM-05). `tools/emu/test-cam.sh:329,339` (reported) | ✅ PASS |
| CAM-09 | Other size ➔ drop, no resize, `FRAME_SIZE_MISMATCH` | `core/test/jpeg_yuyv_test.cpp:108-115` `LargerInOneDimensionIsFrameSizeMismatch`: 1280×800 and 1344×720 each `EXPECT_EQ(…, kFrameSizeMismatch)` and `out` untouched. `:117-122` smaller 480×480. Kills V5 in both dimensions (this cycle) | ✅ PASS |
| CAM-10 | Non-JPEG ➔ `FORMAT_UNSUPPORTED` | `core/test/jpeg_yuyv_test.cpp:124` `png`, `""`. Kills N2 | ✅ PASS |
| CAM-11 | Decode failure ➔ `DECODE_ERROR` | `core/test/jpeg_yuyv_test.cpp:133,141` truncated and non-JPEG bytes. `:148-152` 4:4:4 ➔ `kDecodeError`, but 4:4:4 is also refused by TurboJPEG's stride check, so the subsampling guard at `core/src/jpeg_yuyv.cpp:56` is not pinned: grayscale and 4:4:0 JPEGs are accepted when it is removed (X5, Gap 1) | ❌ GAP |
| CAM-12 | `CAM <pos> DROP <reason>` once per reason change | `core/test/camera_channel_test.cpp:146` exact line list. Kills N8. `tools/emu/test-cam.sh:331-336` (reported) | ✅ PASS |
| CAM-13 | Dropped frame counts as absent | `core/test/camera_channel_test.cpp:138`. `camera_bridge_rostest.cpp:229-259`: `EXPECT_TRUE(… STALE == 1, kStaleMs + 200)` (`:247`), `EXPECT_EQ(1u, Count())` (`:258`). Kills N7 (re-run this cycle) | ✅ PASS |
| CAM-14 | Device fails ➔ `DEVICE_UNAVAILABLE`, retry every 1 s, process alive | `core/test/camera_channel_test.cpp:167,199` open counts at 1099/1100 ms and a single log line (kills N1). `ros/vipee_bridge/test/v4l2_sink_test.cpp:71` adjusted format refused (kills M11). `:83` short write incomplete (kills M10). `tools/emu/test-cam.sh:357,366` (reported) | ✅ PASS |
| CAM-15 | Others and VHAL keep running while one device is down | `tools/emu/test-cam.sh:361` 3 cameras ≥ 8/s + VHAL (reported) | ✅ PASS (reported) |
| CAM-16 | Topic, view, width, height, stale_ms from the selected map | `ros/vipee_bridge/test/bridge_node_rostest.cpp:255-290` exact values from both real maps. Kills N5 | ✅ PASS |
| CAM-17 | Invalid camera section ➔ no camera opens, `CAM MAP_INVALID`, VHAL as before | `bridge_node_rostest.cpp:310-335`: `EXPECT_EQ(0, sinks_made)` after 200 ms of spinning. The only camera log line is `CAM MAP_INVALID DUPLICATE_VIEW`, and telemetry is delivered with status 0. Kills R1 and N6. `core/test/vehicle_map_test.cpp:177` size ≤ 0 (kills N3) | ✅ PASS |
| CAM-18 | No `cameras` ➔ telemetry only, no error | `bridge_node_rostest.cpp:292` | ✅ PASS |
| CAM-19 | After install, FRONTVIEW/REARVIEW/LEFTVIEW/RIGHTVIEW available to `CarEvsManager` | `tools/emu/test-cam.sh:392-396` `get-camera-id $t \| grep -q "/dev/video$i for $t"` for i = 10..13. The format was checked against AOSP `CarShellCommand.java:3640` and live emulator output (Verifier, read-only) | ✅ PASS |
| CAM-20 | Remove ➔ previous HAL and camera config | `tools/emu/test-cam.sh:380-385` all 4 `CAMERA cam_$p` listed, `init.svc.vendor.evs-v4l2 != running` | ✅ PASS (script not run by Verifier) |
| CAM-21 | Enforcing, 0 `avc: denied` | `tools/emu/test-cam.sh:79` `avc_check` in every step (reported) | ✅ PASS (reported) |
| CAM-22 | Bag loop keeps delivering | `core/test/camera_channel_test.cpp:212`. 60 s run over a 24 s bag (reported) | ✅ PASS |
| CAM-23 | Only the newest of two pending frames, silently | `core/test/latest_slot_test.cpp:16`. `camera_bridge_rostest.cpp:261-275` ➔ `EXPECT_NEAR(200, luma.back(), 3)` (`:274`) | ✅ PASS |
| CAM-24 | No client ➔ accept without accumulating memory | `core/test/latest_slot_test.cpp:52` | ✅ PASS |

**Status**: 24/24 ACs have `file:line` evidence. Both cycle-3 gaps are closed (CAM-09, CAM-06). CAM-11 has a new test gap (X5) and CAM-03's p95 helper a minor one (X3).

### Spec-precision notes (carried over, unchanged)

- CAM-12: the same reason is logged again after a valid frame in between. The test pins this reading.
- CAM-02/CAM-19 say `CarEvsManager`. Automation goes through the evsmanagerd probe and `cmd car_service get-camera-id`. Design.md sends the Java client to the manual AVM success criterion.
- CAM-06: the AC gives no deadline for the `STALE` log. The 600 ms bound comes from the story's Independent Test; `camera_bridge_rostest.cpp:214` now pins it.
- CAM-11: the spec says "decode failure"; `design.md:88` narrows it to "4:2:0 and 4:2:2 only, the rest `kDecodeError`". Gap 1 holds the tests to the design.

---

## Discrimination Sensor

Run in a temporary `git worktree` (`scratchpad/wt`, detached at `06608ed`). Host mutants were built in `wt/build` (default preset, `VCPKG_ROOT=~/vcpkg`) and ran the full `ctest --preset default`. ROS mutants were built in the Jazzy container from the worktree into `/ws/ros_test/build_bridge_mut` (`ROS_DOMAIN_ID=78`) and ran all 3 ROS tests. The unmutated baseline passed in both scratch builds. Afterwards the worktree was removed (`git worktree remove --force`, `git worktree prune`) and `build_bridge_mut` was deleted.

**Isolation**: the pre-sensor baseline was ` M .specs/features/cam-evs-slice/tasks.md`. After cleanup the real tree also shows ` M tools/emu/evs-sync.sh`, ` M tools/jazzy/install-vendor.sh` and `?? android/evs/rro/`, written at 22:01–22:02 by a concurrent writer. No mutant touched those paths, and every sensor edit ran inside the worktree. The Verifier left them untouched and does not stage them.

| # | File:line | Mutation | Killed? |
| - | --------- | -------- | ------- |
| V5h | `core/src/jpeg_yuyv.cpp:52` | height `!= height` ➔ `< height` | ✅ `JpegYuyv.LargerInOneDimensionIsFrameSizeMismatch` (and a crash in another test) |
| V5w | `core/src/jpeg_yuyv.cpp:51` | width `!= width` ➔ `< width` | ✅ `JpegYuyv.LargerInOneDimensionIsFrameSizeMismatch` |
| W1 | `ros/vipee_bridge/src/camera_bridge.cpp:16` | tick period 50 ms ➔ 1000 ms | ✅ `SilencedTopicGoesStaleWithoutNewFramesThenLive` (`camera_bridge_rostest.cpp:214`) |
| N7 | `ros/vipee_bridge/src/camera_bridge.cpp:44` | `if (!item) channel.tick(now);` | ✅ `InvalidFramesFasterThanTheTickStillGoStale` |
| R1 | `ros/vipee_bridge/src/bridge_node.cpp:128` | CameraBridge built despite `CAM MAP_INVALID` | ✅ `DuplicateEvsViewLogsCamMapInvalidAndKeepsTelemetry` |
| R3 | `ros/vipee_bridge/src/camera_bridge.cpp:77` | decode+write in the subscription callback | ✅ `StuckCameraDoesNotDelayTelemetry` |
| M10 | `ros/vipee_bridge/src/v4l2_sink.cpp:19` | short write accepted (`written > 0 && len > 0`) | ✅ `V4l2SinkTest.OnlyAFullWriteIsComplete` |
| M11 | `ros/vipee_bridge/src/v4l2_sink.cpp:14-15` | driver-enlarged width/height accepted (`>=`) | ✅ `V4l2SinkTest.DriverAdjustedFormatIsRefused` |
| X1 | `core/src/camera_channel.cpp:74` | STALE at exactly `stale_ms` (`>` ➔ `>=`) | ✅ `CameraChannelTest.StaleOnlyAfterMoreThanStaleMs` |
| X2 | `core/src/camera_channel.cpp:49` | DROP logged on every frame, not once per reason change | ✅ `CameraChannelTest.DropIsLoggedOncePerReasonChange` |
| X3 | `core/src/camera_channel.cpp:84` | p95 rank `(n*95+99)/100` ➔ `(n*95)/100` | ❌ Survived (Gap 2) |
| X4 | `core/src/camera_channel.cpp:59` | write attempted while the sink is closed | ✅ `WriteFailureMarksDeviceUnavailableAndRetriesEachSecond`, `DeviceThatDoesNotOpenIsUnavailableAndRetried` |
| X5 | `core/src/jpeg_yuyv.cpp:56` | subsampling guard removed (only `width % 2` left) | ❌ Survived (Gap 1) |
| X6 | `ros/vipee_bridge/src/camera_bridge.cpp:75` | every camera subscribes to the first camera's topic | ✅ 3 `CameraBridgeTest` (`JpegReachesOnlyTheRightCameraSinkExactlyOnce`, …) |
| X7 | `core/src/camera_channel.cpp:53` | a valid frame does not reset `last_drop_` | ✅ `CameraChannelTest.DropIsLoggedOncePerReasonChange` |

**Sensor depth**: expanded (data-integrity path): 8 required re-runs plus 7 new mutants.
**Result**: 13/15 killed. FAIL ❌.

**X5 is not equivalent.** On the unmutated code a probe `decode("jpeg", makeJpeg(1280, 720, s), …)` for `s` in {GRAY, 4:1:1, 4:4:0} returned `kDecodeError` for all three. Under X5 GRAY (3) and 4:4:0 (5) returned `kNone` (4:1:1 is still refused, by TurboJPEG). The existing 4:4:4 test passes under X5 because TurboJPEG rejects a chroma stride of `width/2` for full-width chroma before our code matters. The probe was discarded with the worktree.

**X3 is not equivalent.** It is deterministic arithmetic: for n = 21, `(21·95+99)/100 = 20` vs `1995/100 = 19`. For n = 1, the mutant gives rank 0 and reads `latencies_ns_[SIZE_MAX]`. The only p95 test (`core/test/camera_channel_test.cpp:223`) uses n = 20, where both give 19.

The forked HAL (`android/evs_hal`) is still not mutation-tested (AOSP code, no host seam). It is covered only by `test-cam.sh stall`/`platform` (reported). W3 and N10 (emulator-only coverage, cycles 2–3) were not re-run; their code is unchanged.

---

## API Correctness (context7 / source)

No production code changed since cycle 2. The earlier findings for TurboJPEG 3, rclcpp Jazzy, V4L2 and `cmd car_service get-camera-id` still apply. X5's probe confirms empirically that `tj3DecompressToYUVPlanes8` accepts GRAY and 4:4:0 with the bridge's strides, so the bridge's own guard is what enforces `design.md:88`.

---

## Code Quality

| Principle | Status |
| --------- | ------ |
| Minimum code / surgical | ✅ Cycle-4 fixes are test-only: one new test, one bounded wait |
| Matches patterns | ✅ Same `makeJpeg`/`SpinUntil` style. No mocks of rclcpp or the HAL |
| Spec-anchored outcome check | ✅ The new tests assert `kFrameSizeMismatch` with output untouched, and STALE within the Independent Test's 600 ms |
| Per-layer coverage | ⚠️ Core: subsampling guard (X5) and p95 rank (X3) not pinned |
| Test reliability (AGENTS.md §3) | ✅ 41/41 reruns green with the new 600 ms bound |
| Guidelines: `AGENTS.md` §3, §3.1, §4 | ✅ Lint clean |

---

## Fix Plans

### Fix 1 (Major): CAM-11 subsampling guard not pinned (X5)

- **Root cause**: `core/test/jpeg_yuyv_test.cpp:148` tests only 4:4:4, which TurboJPEG refuses on its own. Nothing reaches the guard at `core/src/jpeg_yuyv.cpp:56`.
- **Fix task**: in `core/test/jpeg_yuyv_test.cpp`, add `GrayAnd440AreDecodeError`: `makeJpeg(kW, kH, TJSAMP_GRAY)` and `makeJpeg(kW, kH, TJSAMP_440)` must each return `kDecodeError`. Production code stays as is. The test must kill X5.

### Fix 2 (Minor): p95 nearest rank not pinned (X3)

- **Root cause**: `core/test/camera_channel_test.cpp:223` uses 20 samples, where ceil and floor ranks agree.
- **Fix task**: add a `CameraChannelTest` with a single delivered frame (latency L) where `takeStats().p95_latency_ns` equals that frame's latency, and one with 21 frames at 100..2100 ms where p95 is ≥ 2000 ms (the 20th value). The test must kill X3.

---

## Requirement Traceability Update

| Requirement | New Status |
| ----------- | ---------- |
| CAM-01, 02, 04..10, 12..24 | ✅ Verified |
| CAM-03 | ⚠️ Verified (emulator, reported); p95 rank not pinned (Fix 2) |
| CAM-11 | ❌ Needs fix: subsampling guard untested (Fix 1) |

---

## Summary

**Overall**: ❌ Not Ready (1 Major and 1 Minor test-only gap; no production defect)

**Spec-anchored check**: 24/24 ACs have evidence. Cycle-3 gaps V5 and W1 are closed. CAM-11's subsampling guard and CAM-03's p95 rank are not pinned.
**Sensor**: 13/15 killed. V5 (both dimensions), W1, N7, R1, R3, M10, M11 are killed. New survivors: X5 (real), X3 (real).
**Gate**: host 124/124, ROS 3/3, 41/41 reruns, lint clean.

**Next steps**: this was an extra cycle past the bound, so the gaps go back to the user. Fix 1 and Fix 2 are test-only and take a few lines each.
