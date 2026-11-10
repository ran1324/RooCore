# Local dashboard, recorded progress and maintenance

2026-11-10 · [עברית](DASHBOARD-he.md)

`navigation.server` now serves the simulator or a read-only telemetry monitor through the same website. SQLite stores progress, mission summaries, observed usage and user-reported part service. These are software integrations tested with synthetic data and loopback HTTP mocks. No robot was contacted, flashed or physically verified during implementation.

## Run without hardware

```sh
python3 -m navigation.server --source simulation --port 8766
python3 -m navigation.server --source replay --replay-file tests/fixtures/progress-synthetic.jsonl --store recordings/replay.sqlite3 --port 8767
```

Open `http://127.0.0.1:8766` or `http://127.0.0.1:8767`. Simulation is the default. The supplied replay fixture explicitly says `source: simulation`; it is not a robot recording. Replay consumes one recorded snapshot per second, labels the page `replay`, then becomes stale after the configured freshness interval. It does not reconnect to the recorded robot or resume navigation. Simulation controls are rejected in both robot and replay modes.

The server binds only to loopback and rejects unrelated Host/Origin headers. It does not expose a robot command proxy. `--store` chooses the local database, defaulting to ignored `recordings/dashboard.sqlite3`. Recordings can contain a home map; choose their storage and retention deliberately. A separate database is convenient for a demonstration, although every stored record is also partitioned by source and robot ID.

## Future robot observation configuration

The implemented reader makes only `GET /progress`, using a server-held bearer token. URL and credentials are never supplied by the browser, returned in dashboard data, or written to recordings. Redirects are disabled. The server supports HTTP for the existing trusted-LAN firmware endpoint and HTTPS if the endpoint supports it; this is not a remote deployment configuration.

Set these in the server process environment using the local secret/configuration mechanism:

| Variable | Meaning |
|---|---|
| `ROOCORE_ROBOT_URL` | Exact robot HTTP(S) origin, with no path/query/user credentials |
| `ROOCORE_ROBOT_TOKEN` | Existing firmware bearer token, at least 24 characters |
| `ROOCORE_ROBOT_ID` | Operator-assigned identity of the actual robot |
| `ROOCORE_BATTERY_ID` | Identity of the installed pack, changed when it is replaced |
| `ROOCORE_WORKLOAD_ID` | Explicit comparable floor/task/attachments/control profile |

The first three are needed to select `--source robot`; the last two and `--health-policy /path/to/policy.json` are needed for battery trend analysis. Leaving those unset shows `unconfigured`, rather than assuming an installed pack or diagnostic thresholds. `--stale-after 5` is the default host display/usage continuity window in seconds, not an electrical or battery limit. The analyzer additionally applies its explicitly configured battery-sample freshness limit.

Robot mode was implemented but not used on hardware here. Existing firmware commissioning, sensor verification and physical safety conditions still apply to the firmware; the dashboard does not change them. See [battery analysis](BATTERY_HEALTH.md) for the required policy fields. A policy file contains exactly those fields. For synthetic test reproduction only, this creates the same **uncalibrated synthetic policy** used by the tests:

```sh
python3 -c 'import json; from dataclasses import asdict; from tests.test_battery_health import policy; print(json.dumps(asdict(policy())))' > recordings/synthetic-policy.json
```

That file is not a recommendation for a real pack. No calibrated battery policy is shipped.

## Report contract

`GET /api/state` retains the simulator's flat map fields and adds `source`, `read_only`, `identity`, `freshness`, `progress`, `battery_health`, `history` and `maintenance`. `GET /api/health`, `/api/history` and `/api/maintenance` return their respective portions. `/api/checkpoint` exports the simulator checkpoint or the last read-only firmware snapshot; waiting for the first firmware frame returns HTTP 409.

The firmware's schema-1 `/progress` includes boot/mission/map identity, monotonic sequence/revision, MCU time, a bounded occupancy grid, packed central/side coverage, pose and a status snapshot. `pass_index` is zero-based; legacy `pass` is one-based. The adapter validates shape and counter order, requires a new map identity when dimensions/origin/resolution change, separates old boots, freezes invalid localization and splits traces at missing observations/frame changes. A movement larger than one grid cell between snapshots starts a new drawn segment; this is a display continuity rule, not a physical motion limit.

Coverage means the current pass's central-brush coverage of **observed floor**. Unknown floor is not added to its denominator. Side-only coverage and exclusions remain separate. Program completion is a separate boolean and can coexist with uncovered known floor. `reported_active_s` is the firmware's active mission time, including return/docking/recovery and excluding charging/pauses; it is not pure cleaning time. `reported_cleaning_s` remains null unless a separate supported counter is present. Observed command-time counters skip telemetry gaps and do not measure mechanical revolutions, wear or the robot's complete lifetime usage.

Battery envelopes use explicit `battery_sample_ms` freshness and `battery_valid`, plus the confirmed, mission/boot/time-bound return event. A generic status reason cannot establish the cause. Repeated cached frames do not refresh the live marker. Connection failures persist discontinuity records, so replaying battery history preserves exclusions. Idle observation identities (`mission_active: false`) do not create cleaning missions or battery comparison legs. Replay health is historical and remains labelled replay; synthetic input never produces real-battery health alerts.

## Maintenance API and provenance

`POST /api/maintenance/settings` accepts `{"pets":true}`. `POST /api/maintenance/event` accepts:

```json
{"part":"filter","action":"replaced","date":"2026-11-10","identity":"optional user-recorded part identity"}
```

Parts are `bin`, `filter`, `main_brushes`, `front_caster`, `side_brush` and `cliff_sensors`, plus `charging_contacts`. Actions are `installed`, `cleaned`, `replaced` and `snoozed`. An installation/replacement records a new age and usage baseline; cleaning does not reset replacement age. A later same-action entry can correct the effective date, while earlier entries remain in the event log. Dates and identities are user reports. Unknown installation dates remain unknown; backdating cannot reconstruct unrecorded usage. Maintenance actions update only local storage, including when viewing a real robot.

Reminders link the [iRobot legacy care table](https://support.irobot.co.uk/articles/en_GB/Knowledge/11179) and [600-series procedure](https://homesupport.irobot.com/articles/en_US/Knowledge/8247). Weekly care becomes twice weekly with pets for the filter/main brushes; a date-only reminder after three days is a RooCore scheduling approximation. Replacement ranges remain ranges: main-brush review begins at six months and its table window ends at twelve months. No replacement interval is fabricated for parts with an unspecified interval. The filter must not be washed; brush care includes bearings. `wear_percent` is always null.

## Validation

```sh
python3 -m unittest tests.test_monitor tests.test_battery_health -v
```

Tests cover observed versus completed coverage, invalid pose and stale/duplicate/out-of-order frames, fixed map frames, counter wrap, idle records, gap persistence, configured battery evidence, source-separated history/parts, actual simulator usage deltas, and read-only HTTP behavior through localhost mocks. They do not establish physical packet support, installed battery chemistry/capacity, dock sensing or charging behavior.
