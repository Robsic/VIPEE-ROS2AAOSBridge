# LESSONS - auto-maintained by scripts/lessons.py

> Machine-owned. Do NOT hand-edit. Changes are overwritten on the next `lessons.py` write.
> Canonical state lives in `.specs/lessons.json`. Edit lessons only via the script.
> promote_threshold=2 distinct features · window_days=45 · quarantine_threshold=2

## Confirmed (load these at Specify/Design)

Corroborated across multiple features. Safe to apply as guidance.

_none_

## Candidates (under observation - do NOT load as guidance yet)

Seen once or not yet corroborated. Tracked, not trusted.

### L-001 - Exercise every member of an allow-list in unit tests, not a representative subset
- signal: `surviving_mutant` · recurrence: 1 feature(s) · scope: `vhal` · harmful: 0
- features: ct-speed-slice
- evidence: M21 android/vhal/include/vipee/vhal_ingest.h:140 (vhal)
- last seen: 2026-11-02T10:31:07Z

### L-002 - Test each input that refreshes a liveness watchdog on its own, not only the heartbeat
- signal: `surviving_mutant` · recurrence: 1 feature(s) · scope: `vhal` · harmful: 0
- features: ct-speed-slice
- evidence: M18 android/vhal/include/vipee/vhal_ingest.h:149 (vhal)
- last seen: 2026-11-02T10:31:07Z

### L-003 - Test the first event after a reconnect or reset against rate-limiting and deduplication state
- signal: `surviving_mutant` · recurrence: 1 feature(s) · scope: `vhal` · harmful: 0
- features: ct-speed-slice
- evidence: M20 android/vhal/include/vipee/vhal_ingest.h:168 (vhal)
- last seen: 2026-11-02T10:31:07Z

### L-004 - Cover every failure branch an AC lists, including errors raised during framework startup before the app's own handling runs
- signal: `ac_gap` · recurrence: 1 feature(s) · scope: `ros` · harmful: 0
- features: ct-speed-slice
- evidence: CTS-23 ros/vipee_bridge/src/main.cpp:172 (ros)
- last seen: 2026-11-02T10:31:07Z

### L-005 - End-to-end checks of a success criterion that names several signals must assert every signal, not one representative
- signal: `ac_gap` · recurrence: 1 feature(s) · scope: `e2e` · harmful: 0
- features: ct-speed-slice
- evidence: CTS-10 tools/emu/test-e2e.sh:63 (e2e)
- last seen: 2026-11-02T10:31:07Z

### L-006 - When status and reason are separate fields, the spec must say whether a reason-only change is a logged transition
- signal: `spec_precision_gap` · recurrence: 1 feature(s) · scope: `core` · harmful: 0
- features: ct-speed-slice
- evidence: CTS-19 M7 core/src/bridge.cpp:95 (core)
- last seen: 2026-11-02T10:31:07Z

### L-007 - A test that a slow path does not block another must start its clock before the slow work is triggered and assert while it is still running; never spin the executor between triggering the stall and starting the measurement.
- signal: `surviving_mutant` · recurrence: 1 feature(s) · scope: `ros-tests` · harmful: 0
- features: cam-evs-slice
- evidence: R3 ros/vipee_bridge/src/camera_bridge.cpp:77; ros/vipee_bridge/test/camera_bridge_rostest.cpp:243 (ros-tests)
- last seen: 2026-11-02T23:57:52Z

### L-008 - When an AC says an invalid config opens nothing, assert the absence with a counting factory or sink, not only the error log line.
- signal: `surviving_mutant` · recurrence: 1 feature(s) · scope: `ros-tests` · harmful: 0
- features: cam-evs-slice
- evidence: R1 ros/vipee_bridge/src/bridge_node.cpp:128; ros/vipee_bridge/test/bridge_node_rostest.cpp:293 (ros-tests)
- last seen: 2026-11-02T23:57:52Z

### L-009 - Device sinks whose success path needs real hardware should keep format and write-length acceptance in pure functions with host unit tests.
- signal: `surviving_mutant` · recurrence: 1 feature(s) · scope: `device-io` · harmful: 0
- features: cam-evs-slice
- evidence: M10/M11 ros/vipee_bridge/src/v4l2_sink.cpp:132,147 (device-io)
- last seen: 2026-11-02T23:57:52Z

### L-010 - An install AC naming the consumer-facing identifiers must be asserted at that layer, not only at the layer below.
- signal: `ac_gap` · recurrence: 1 feature(s) · scope: `emulator-tests` · harmful: 0
- features: cam-evs-slice
- evidence: CAM-19 tools/emu/test-cam.sh:380 (emulator-tests)
- last seen: 2026-11-02T23:57:58Z

### L-011 - In a cross-thread test, wait on the asserted side effect itself, never on a log line the worker emits before producing it.
- signal: `gate_fail` · recurrence: 1 feature(s) · scope: `ros-tests` · harmful: 0
- features: cam-evs-slice
- evidence: ros/vipee_bridge/test/camera_bridge_rostest.cpp:219 (ros-tests)
- last seen: 2026-11-03T00:26:35Z

### L-012 - Test a periodic watchdog while input arrives faster than its period, so a loop that only checks on idle wakeups is caught.
- signal: `surviving_mutant` · recurrence: 1 feature(s) · scope: `ros-tests` · harmful: 0
- features: cam-evs-slice
- evidence: N7 ros/vipee_bridge/src/camera_bridge.cpp:44 (ros-tests)
- last seen: 2026-11-03T00:26:35Z

### L-013 - Test size-mismatch rejection with inputs larger than the expected size in each dimension, not only smaller ones
- signal: `surviving_mutant` · recurrence: 1 feature(s) · scope: `core` · harmful: 0
- features: cam-evs-slice
- evidence: V5 core/src/jpeg_yuyv.cpp:52 (core)
- last seen: 2026-11-03T00:47:07Z

### L-014 - Bound the wait for a timeout-driven state change by the spec deadline, not by a generic test timeout
- signal: `surviving_mutant` · recurrence: 1 feature(s) · scope: `ros-tests` · harmful: 0
- features: cam-evs-slice
- evidence: W1 ros/vipee_bridge/src/camera_bridge.cpp:16 (ros-tests)
- last seen: 2026-11-03T00:47:07Z

### L-015 - Test a rejected input variant that the underlying library would otherwise accept; a variant the library already refuses never reaches your own guard
- signal: `surviving_mutant` · recurrence: 1 feature(s) · scope: `decode` · harmful: 0
- features: cam-evs-slice
- evidence: X5 core/src/jpeg_yuyv.cpp:56 (decode)
- last seen: 2026-11-03T01:09:03Z

### L-016 - Test percentile and rank math at sample counts where floor and ceil rounding differ, including n = 1
- signal: `surviving_mutant` · recurrence: 1 feature(s) · scope: `stats` · harmful: 0
- features: cam-evs-slice
- evidence: X3 core/src/camera_channel.cpp:84 (stats)
- last seen: 2026-11-03T01:09:03Z

### L-017 - Commit the RED fixtures of a static gate script as an automated test, so weakening the gate fails a suite
- signal: `surviving_mutant` · recurrence: 1 feature(s) · scope: `tools` · harmful: 0
- features: rpi-image
- evidence: tools/rpi/check-image.sh:82 (C1-C3) (tools)
- last seen: 2026-11-06T20:44:23Z

### L-018 - Before a handoff reuses an emulator test script on real hardware, check every step the script runs for pushes or property writes to the device
- signal: `ac_gap` · recurrence: 1 feature(s) · scope: `handoff` · harmful: 0
- features: rpi-image
- evidence: Refs/handoff-rpi-image.md:133 (RPI-09, G1) (handoff)
- last seen: 2026-11-06T20:44:23Z

### L-019 - Assert every clause of a compound acceptance criterion, not only the log code
- signal: `ac_gap` · recurrence: 1 feature(s) · scope: `e2e` · harmful: 0
- features: rpi-image
- evidence: tools/emu/test-e2e.sh:170-177 (RPI-05, G3) (e2e)
- last seen: 2026-11-06T20:44:23Z

### L-020 - State the observable evidence for connection-established criteria in the spec
- signal: `spec_precision_gap` · recurrence: 1 feature(s) · scope: `spec` · harmful: 0
- features: rpi-image
- evidence: spec.md RPI-06 (G5) (spec)
- last seen: 2026-11-06T20:44:23Z

## Quarantined (failed when applied - ignore)

A confirmed lesson that recurred alongside failure. Kept for the maintainer to review.

_none_
