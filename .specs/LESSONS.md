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

## Quarantined (failed when applied - ignore)

A confirmed lesson that recurred alongside failure. Kept for the maintainer to review.

_none_
