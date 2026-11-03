# cam-evs-slice Validation

**Date**: 2026-11-02
**Spec**: `.specs/features/cam-evs-slice/spec.md`
**Diff range**: `1f55d54..HEAD` (cycle-5 fixes `c22e7ce`, `9a7e1f9`)
**Verifier**: independent sub-agent, cycle 5 (author ≠ verifier)
**Scope**: by the user's decision, cycle 5 is a scoped check: confirm the two cycle-4 gaps are closed and gates are green. No new mutants were hunted.
**Verdict**: PASS

Both cycle-4 gaps are closed. The new tests assert the spec/design outcome (CAM-11 via design.md:88: non-4:2:0/4:2:2 JPEG ➔ `kDecodeError`, output untouched; CAM-03 STATS: nearest-rank ceil(0.95 n) p95), not the implementation. Re-applying X5 and X3 in an isolated worktree, each is killed by the host suite.

---

## Cycle History

| Cycle | Commit | Verdict | Gaps / survivors |
| ----- | ------ | ------- | ---------------- |
| 1 | `1bf5f6e` | FAIL | CAM-17 not asserted (R1). CAM-05/08 ROS test not discriminating (R3). V4l2Sink success path untested (M10, M11). CAM-19 not asserted. CAM-20 partial |
| 2 | `91b0d6f` | FAIL | Cycle-1 gaps closed. Flaky race at `camera_bridge_rostest.cpp:219`; survivor N7 |
| 3 | `3958768` | FAIL | Cycle-2 gaps closed. Survivors V5 (CAM-09), W1 (CAM-06 timing) |
| 4 | `47b84d5` | FAIL | Cycle-3 gaps closed. Survivors X5 (`jpeg_yuyv.cpp:56`, CAM-11), X3 (`camera_channel.cpp:84`, CAM-03) |
| 5 | this report | PASS | Scoped (user decision). X5, X3 killed by `c22e7ce`, `9a7e1f9` |

---

## Gate Check

- **Host**: `cmake --preset default && cmake --build --preset default && ctest --preset default` ➔ 127/127 passed (124 + 3 new tests).
- **ROS**: `tools/jazzy/run.sh /bridge/tools/jazzy/test-ros.sh` ➔ 3/3 passed.
- **Lint**: `clang-format --dry-run --Werror` clean on `core/test/jpeg_yuyv_test.cpp` and `core/test/camera_channel_test.cpp`.
- **Test integrity**: `c22e7ce` (+10) and `9a7e1f9` (+19) only add tests; nothing removed or weakened. No production code changed.
- **Emulator**: not used (no Android-side change).

---

## Spec-Anchored Acceptance Criteria


Since cycle 3 (`31f3262`) only two test files changed: `core/test/jpeg_yuyv_test.cpp` (+9 lines at :108, so later lines in that file shift by 9) and `ros/vipee_bridge/test/camera_bridge_rostest.cpp` (+3 lines at :212, so later lines shift by 3). The citations below use the current line numbers. No production file changed (`git diff --stat 31f3262..HEAD`).

| ID | Spec-defined outcome | Evidence (`file:line` + assertion) | Result |
| -- | -------------------- | ---------------------------------- | ------ |
| CAM-01 | JPEG 1280×720 on `<pos>` reaches the mapped EVS camera in 1280×720 | `core/test/camera_channel_test.cpp:94` YUYV bytes equal and size `kW*kH*2`. `ros/vipee_bridge/test/camera_bridge_rostest.cpp:195-206` `EXPECT_EQ(vector{kFrameBytes}, sinks_["rear"]->Lens())`, other sinks 0. `tools/emu/test-cam.sh:238-241` `check_rate` width×height (reported) | ✅ PASS |
| CAM-02 | ≥ 8 frames/s per camera on a client | `tools/emu/test-cam.sh:268` `check_rate … $BAG_S bag` per device (reported). The client is the evsmanagerd probe, not `CarEvsManager` (see notes) | ✅ PASS (reported) |
| CAM-03 | ≤ 100 ms from receipt to device, measured in the ROS process | `tools/emu/test-cam.sh:273-278` `exit !(max <= 100)` over every STATS p95 (reported). `core/test/camera_channel_test.cpp:223-245` p95 for n = 20; `:255` n = 1 ➔ p95 in [100, 200) ms; `:262` n = 21 at 100..2100 ms ➔ p95 in [2000, 2100) ms, the ceil(0.95 n) rank. Kills X3. N10 (rx stamped 0) is caught only by the emulator check | ✅ PASS (reported) |
| CAM-04 | Each frame at most once, no repeat | `core/test/camera_channel_test.cpp:107` 1 write after 40 ticks. `camera_bridge_rostest.cpp:199-200` one write after 400 ms. `tools/emu/test-cam.sh:164` `last_frame_ms_ago >= 2000` on the HAL fork (reported) | ✅ PASS |
| CAM-05 | ct-speed-slice ≤ 200 ms / ≤ 600 ms with 4 cameras on | `camera_bridge_rostest.cpp:277-296`: `start` taken before the back-to-back publish, then `EXPECT_LT(Clock::now() - start, 500ms)` (`:295`) while `EXPECT_EQ(0u, sinks_["front"]->Count())` (`:296`, sink still inside the 1.5 s write). Kills R3 (re-run this cycle). `tools/emu/test-e2e.sh:23,29,162` (reported 121 ms / 561 ms) | ✅ PASS |
| CAM-06 | > stale_ms without a valid frame ➔ stop, `CAM <pos> STALE` (Independent Test: within 600 ms) | `core/test/camera_channel_test.cpp:113` none at 500 ms, STALE at 500 ms + 1 ns. `camera_bridge_rostest.cpp:213-217`: `first_write` taken when the write is seen, `ASSERT_TRUE(SpinUntil(… STALE == 1, 600))` (`:214`), `EXPECT_GE(… - first_write, 450ms)` (`:216`), `EXPECT_EQ(1u, Count())` (`:217`). Kills W1 (`fc5aac2`/`06608ed` cycle). 41/41 reruns green | ✅ PASS |
| CAM-07 | Valid frame in STALE ➔ delivered, `CAM <pos> LIVE`, no reopen | `core/test/camera_channel_test.cpp:122`. `camera_bridge_rostest.cpp:219-226`: waits for `LIVE == 2` and `Count() == 2`, spins 200 ms, then `EXPECT_EQ(2u, Count())` (`:226`). `tools/emu/test-cam.sh:306` (reported) | ✅ PASS |
| CAM-08 | A stopped camera does not interrupt the others or the VHAL | `camera_bridge_rostest.cpp:277-296` (as CAM-05). `tools/emu/test-cam.sh:329,339` (reported) | ✅ PASS |
| CAM-09 | Other size ➔ drop, no resize, `FRAME_SIZE_MISMATCH` | `core/test/jpeg_yuyv_test.cpp:108-115` `LargerInOneDimensionIsFrameSizeMismatch`: 1280×800 and 1344×720 each `EXPECT_EQ(…, kFrameSizeMismatch)` and `out` untouched. `:117-122` smaller 480×480. Kills V5 in both dimensions (this cycle) | ✅ PASS |
| CAM-10 | Non-JPEG ➔ `FORMAT_UNSUPPORTED` | `core/test/jpeg_yuyv_test.cpp:124` `png`, `""`. Kills N2 | ✅ PASS |
| CAM-11 | Decode failure ➔ `DECODE_ERROR` | `core/test/jpeg_yuyv_test.cpp:133,141` truncated and non-JPEG bytes. `:148-152` 4:4:4 ➔ `kDecodeError`, `:154-162` `TJSAMP_GRAY` and `TJSAMP_440` ➔ `kDecodeError` with `out` untouched, the design.md:88 outcome (only 4:2:0/4:2:2 accepted). Kills X5 | ✅ PASS |
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

**Status**: 24/24 ACs have `file:line` evidence. Both cycle-3 gaps are closed (CAM-09, CAM-06). Cycle-4 gaps X5 (CAM-11) and X3 (CAM-03) are closed by `c22e7ce` and `9a7e1f9`.

### Spec-precision notes (carried over, unchanged)

- CAM-12: the same reason is logged again after a valid frame in between. The test pins this reading.
- CAM-02/CAM-19 say `CarEvsManager`. Automation goes through the evsmanagerd probe and `cmd car_service get-camera-id`. Design.md sends the Java client to the manual AVM success criterion.
- CAM-06: the AC gives no deadline for the `STALE` log. The 600 ms bound comes from the story's Independent Test; `camera_bridge_rostest.cpp:214` now pins it.
- CAM-11: the spec says "decode failure"; `design.md:88` narrows it to "4:2:0 and 4:2:2 only, the rest `kDecodeError`". Gap 1 holds the tests to the design.

---

## Discrimination Sensor

Cycle 5: worktree `scratchpad/wt5` detached at HEAD, default preset build, full `ctest --preset default` per mutant. X5 (guard reduced to `width % 2 != 0`) ➔ killed by `JpegYuyv.GrayscaleAndFourFortyJpegsAreDecodeErrorAndLeaveOutAlone`. X3 (rank `(n*95)/100`) ➔ killed by `CameraChannelTest.P95OfOneFrameIsItsLatency` and `P95Of21FramesIsTheTwentiethSmallest`. Worktree removed and pruned; real-tree `git status --porcelain` equals the baseline (` M .specs/features/cam-evs-slice/tasks.md`).

**Cycle 5 result**: 2/2 killed. PASS

Cumulative mutant list (cycles 1–5, current status):

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
| X3 | `core/src/camera_channel.cpp:84` | p95 rank `(n*95+99)/100` ➔ `(n*95)/100` | ✅ cycle 5: `P95OfOneFrameIsItsLatency`, `P95Of21FramesIsTheTwentiethSmallest` |
| X4 | `core/src/camera_channel.cpp:59` | write attempted while the sink is closed | ✅ `WriteFailureMarksDeviceUnavailableAndRetriesEachSecond`, `DeviceThatDoesNotOpenIsUnavailableAndRetried` |
| X5 | `core/src/jpeg_yuyv.cpp:56` | subsampling guard removed (only `width % 2` left) | ✅ cycle 5: `GrayscaleAndFourFortyJpegsAreDecodeErrorAndLeaveOutAlone` |
| X6 | `ros/vipee_bridge/src/camera_bridge.cpp:75` | every camera subscribes to the first camera's topic | ✅ 3 `CameraBridgeTest` (`JpegReachesOnlyTheRightCameraSinkExactlyOnce`, …) |
| X7 | `core/src/camera_channel.cpp:53` | a valid frame does not reset `last_drop_` | ✅ `CameraChannelTest.DropIsLoggedOncePerReasonChange` |

Earlier-cycle mutants not listed (N1, N2, N3, N5, N6, N8) were killed in cycles 1–2 and their code is unchanged. W3 and N10 have emulator-only coverage. The forked HAL (`android/evs_hal`) is not mutation-tested (AOSP code, no host seam).

---

## Requirement Traceability Update

| Requirement | New Status |
| ----------- | ---------- |
| CAM-01..24 | ✅ Verified (CAM-02, 03, 15, 21 partly via emulator results reported by the phase-4 worker) |

---

## Summary

**Overall**: ✅ Ready

**Spec-anchored check**: 24/24 ACs have evidence; cycle-4 gaps closed.
**Sensor**: cycle 5 2/2 killed (X5, X3); cumulative 15/15 listed mutants killed.
**Gate**: host 127/127, ROS 3/3, lint clean.
