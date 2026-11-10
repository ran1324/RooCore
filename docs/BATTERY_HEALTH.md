# Battery runtime and degradation analysis

2026-11-10 · [עברית](BATTERY_HEALTH-he.md)

Implemented: [`navigation/battery_health.py`](../navigation/battery_health.py), a read-only Python analyzer and JSONL replay CLI. It performs no network requests, drives no motors and changes no charging or cutoff settings. The [dashboard monitor](DASHBOARD.md) now integrates it with recorded or read-only `/progress` telemetry, durable SQLite history and a website report. Enabling real analysis requires operator-supplied identity and policy. The simulator displays its synthetic status without physical health alerts. This integration is tested software using synthetic inputs and local HTTP mocks; no physical battery has been measured and no robot was contacted.

## What the result means

- **Insufficient data:** no reliable comparable history yet. A short first run cannot establish battery degradation.
- **Needs measurement:** repeated battery-driven charging stops, a sharp new runtime drop, or an abrupt charge-reading jump. Check charging, brush load, contacts, changed operating conditions and battery readings before blaming the pack.
- **Suspected degradation:** several distinct comparable missions have shorter runtime than the earlier observed baseline. This supports investigation; it is not a capacity measurement or a certain failed battery.
- **No decline detected:** the available comparable runtime evidence did not cross the configured alert criteria. This does not certify battery health.

The report deliberately returns `capacity_percent: null` and `replacement_required: null`. Its runtime ratio is **not** a state-of-health percentage. A charge-percentage jump is treated as a measurement anomaly and excludes the affected leg from runtime comparisons.

## Evidence and comparison

A discharge leg starts when working/undocking is observed after dock contact and charging. This establishes an observed charging episode, **not** a full charge. Work time runs until returning (`state=2`), but a separate confirmed `return_cause` event must establish that the return was battery-driven. Neither `reason=7` nor a low battery percentage establishes that cause. Subsequent dock contact plus charging confirms a stop for recharging; merely requesting Home does not. Separate legs retain the same mission ID across charge/resume.

Runtime comparisons require:

- Same robot, installed battery identity, workload description, cleaning profile and one unchanged observed nonzero brush/vacuum PWM pair.
- Starting and return charge within the caller's comparison tolerance. A departure at 80% is not compared blindly with one at 100%; no linear extrapolation to a full battery is performed.
- Continuous fresh battery telemetry, valid navigation, an observed departure and a confirmed battery-return event. Pauses, manual/unknown returns, safety events, changed cleaning power, missing data, reboot and ambiguous clock resets exclude a leg.
- Distinct missions. Repeated recharge legs from one mission cannot manufacture independent baseline/trend evidence.

The first configured number of matching missions establish an **observed reference**, not a known-good factory baseline. Later runtime is divided by their median. A single ratio below the configured abrupt limit raises an anomaly. Sustained decline requires every selected recent mission to cross the decline limit. Replacing the battery requires a new `battery_id`, so its history remains separate. Simulated records are retained as such and never generate real-battery health alerts.

All policy fields are required; the module ships **no physical or diagnostic threshold defaults**:

| Field | Role |
|---|---|
| `max_gap_ms`, `max_sensor_age_ms` | Collector continuity and sensor-freshness contracts |
| `charge_tolerance_pct` | Maximum difference in starting/return charge for comparison |
| `baseline_missions`, `recent_missions` | Independent mission counts for reference and repeated decline |
| `decline_ratio`, `abrupt_ratio` | Configured runtime-ratio alert limits |
| `repeated_recharges` | Charging stops within one mission that prompt investigation |
| `max_step_pct` | Largest accepted change between adjacent fresh charge readings |

Choose these from the logging cadence, measurement uncertainty and an explicit alert criterion, then validate against labelled runs. Values in the tests are synthetic software fixtures. None is an iRobot battery specification. The report includes the policy and its origin for audit.

## Input and server integration

Each JSONL line is a schema-1 envelope. Required top-level fields are `source` (`robot` or `simulation`), `robot_id`, `battery_id`, `mission_id`, `boot_id`, `workload_id`, `profile`, `battery_valid` and `status`. IDs are nonempty strings. `workload_id` must distinguish relevant changes such as floor/task scope, attachment configuration and control policy. Battery chemistry/type belongs in the battery inventory; the ID must change on replacement, not on reboot.

Required `status` fields are `now`, `sensors_ms`, `battery`, `state`, `reason`, `brush_pwm`, `vacuum_pwm`, `docked`, `charging`, `localized`, `scan_valid`, `cliff` and `wheel_drop`. `undocking` and `battery_mv` are optional. MCU times are unsigned 32-bit milliseconds; rollover is handled. Invalid battery records may use `battery: null` with `battery_valid: false`. Invalid schema fails with its line number rather than silently filling values.

The returning record can include a top-level `return_cause` object:

```json
{"cause":"battery","provenance":"firmware_mission_event","confirmed":true,"mission_id":"mission-42","boot_id":"boot-8","at_ms":300000}
```

`cause` is `battery`, `manual`, `completed` or `unknown`; `provenance` is `firmware_mission_event`, `confirmed_collector_event` or `unknown`. All six fields are required when the object is present. The event must be confirmed, match this record's mission and boot, occur during this leg, and be no older than `max_gap_ms` at the returning snapshot. Missing, stale, mismatched or unconfirmed evidence produces `unknown_return`, which cannot support battery runtime comparisons or recharge counts. The report retains supplied event metadata for audit.

The firmware now records an explicit `ReturnCause` and exposes a mission/boot/time-bound `return_cause` in `/status` and `/progress`. This software path still needs physical verification. A confirmed collector event likewise requires a verified mission transition and its actual cause; a low percentage, `status.reason`, or HTTP acceptance of a queued command is insufficient. Both the battery path and the manual Home handler use [`goHome()`](../lib/RooNavigation/src/RooNavigation.h); the general reason code can remain unchanged, so it is deliberately not used as return provenance. Old recordings without the explicit event remain ineligible even when they contain `state=2, reason=7`.

The monitor reads firmware boot/mission identity, `battery_valid` and the explicit `battery_sample_ms`; it obtains robot/installed-battery/workload identity from server configuration. Its generic `sensors_ms` alone does not independently verify packet 25/26 or the installed pack. Do not invent identity/validity from a page reload, server receipt time, simulator or battery percentage. Missing configuration leaves health unconfigured, and a missing battery timestamp invalidates the sample. Duplicate cached snapshots add no runtime; out-of-order samples do not extend the clock. Network errors and server restarts interrupt comparisons; stored discontinuity records preserve exclusions during history replay. Physical packet support and installed-pack identity remain unverified.

The standalone analyzer keeps history in memory; callers can persist append-only JSONL. The dashboard persists the same envelopes in SQLite and replays them after restart, then marks the restart boundary as a discontinuity before accepting new samples. Replaying the same ordered log reconstructs the report. An unfinished leg remains open and contributes no completed-run comparison. Keep recordings in the ignored `recordings/` directory. Reports cover the supplied recording, including its historical measurement warnings; a UI should show dates/mission IDs and not present old alerts as a newly measured fault.

```python
from navigation.battery_health import BatteryHealth, Policy

analyzer = BatteryHealth(Policy(**configured_policy))
analyzer.feed(verified_telemetry_envelope)
dashboard_data = analyzer.report()
```

```sh
python3 -m navigation.battery_health recordings/battery.jsonl --policy /path/to/analysis-policy.json
python3 -m unittest discover -s tests -p test_battery_health.py -v
```

These commands read local files and run synthetic tests; neither connects to the robot. Store the policy as a JSON object with all fields in the table. The CLI writes its JSON report to stdout and never modifies its input files.

## Nickel battery care and the one-hour expectation

iRobot's [feature table, Runtime on hardwood](https://support.irobot.de/articles/en_US/Knowledge/163) lists 60 minutes for the legacy Roomba 620, while stating that its lab-tested runtime varies. That is context, not a failure threshold: our add-on load, cleaning settings and early return reserve differ from an unspecified manufacturer's run. The user's nickel description remains **user-reported** until the installed pack label/model is recorded. The [battery compatibility table](https://support.irobot.co.uk/articles/en_GB/Knowledge/21019) contains NiMH and replacement variants; model number alone does not establish the currently installed chemistry.

iRobot's [battery maintenance guidance](https://support.irobot.co.uk/articles/en_US/Knowledge/7273) recommends prompt recharging, keeping the robot on its powered base when unused, a clean robot to avoid excess load, and room-temperature charging away from sunlight. Useful server features are reminders and explanations tied to those conditions, recording replacement dates, and detecting changes in comparable runtime. This source does **not** establish an 80% charging ceiling as a NiMH life-extension policy.

Native charging completion, temperature control and any model-specific refresh charging remain the original charger's responsibility. Do not automate deep discharge, reconditioning or charger interruption from this report. The current 80% mission-resume condition is not a charger stop command. iRobot's [charging errors 6/7 guidance](https://homesupport.irobot.com/articles/en_US/Knowledge/2440) gives room-temperature environmental advice; it is not a threshold for the OI battery-temperature sensor. No automatic temperature threshold is encoded here. Temperature/charge-capacity diagnostics need verified packet support, units and exact installed-pack limits first.

## Validation

The dedicated 22-test suite covers abrupt versus sustained decline, recovery, repeated charging versus independent missions, manual completion/Home, current firmware's ambiguous return reasons, explicit confirmed causes and invalid event provenance/timing, battery replacement, different start/end charge, workload/PWM change, unobserved starts, safety/localization interruption, missing/stale readings, invalidation of a cached snapshot after connection loss, duplication, reboot, delayed old-boot data, clock rollover/reset, charge-reading jumps, simulated data, and file/CLI replay. It does not validate physical battery capacity, charging behavior, packet availability or the reason for a real runtime decline.
