# RooCore battery monitoring and learning

14 November 2026 · Roomba 620 / plain XIAO ESP32S3 · Local firmware implementation; no flashing or robot validation.

[עברית](BATTERY_LEARNING-he.md) · [Power and fallback policy](POWER_MANAGEMENT.md) · [Electrical plan](assembly/en/POWER_DOCKING_PLAN.md).

## Recorded data

Firmware integrates OI battery current into mAh and voltage/current into mWh, keeping charging and discharge separate. It records temperature, charging state/source, gauge charge/capacity, observed time and missing time. Voltage is never converted directly into remaining mAh. The user-reported 3200mAh label is a rating, not measured available capacity.

Retention is the latest 32 cycle summaries and 256 traces, plus cumulative totals. Traces are recorded every 30 seconds and at events such as cycle completion or first voltage threshold crossings. This is not a complete UART log. Summaries include consumption, charging, voltage/temperature extrema, gaps, gauge changes and docking-trip results. Threshold snapshots at 13.8/13.5/13.0/12.5V are first instantaneous crossings, not calibrated voltage-to-SOC points.

A cycle starts on discharge without a charging source and ends after five seconds of stationary Passive charging state 2/3. It can end at partial recharge: deep discharge and reaching 12.5V are unnecessary. Charging fault and reboot close the cycle with invalidation flags.

Native recording with `profile=base` can accumulate battery telemetry but has no fresh encoders. Qualified cycle completion and full anchoring require packets 43/44 verified on this 620: the normal firmware stream after encoder qualification, or separately enabled/qualified `extended` recording. Full Create 2 compatibility is not assumed.

## Capacity and remaining charge

A continuous discharge window spanning at least 25 percentage points of reported SOC permits:

`capacity_candidate_mAh = integrated_net_discharge_mAh × OI_capacity_mAh / OI_charge_drop_mAh`

This estimate depends on the Roomba SOC gauge and current measurement. It can correct the mAh scale if the SOC fraction is reliable; it is not an independent chemical capacity measurement or proof of gauge accuracy. The rolling median reduces single-cycle influence; the range and contributing cycle counts remain visible. Unqualified measurements produce advisory results only.

A gap over 200ms, invalid telemetry, reboot, unexplained gauge increase, OI capacity change or partial charging within the discharge window excludes the capacity fit. Missing current is not interpolated and duplicate cached samples are not integrated twice. Zero crossings split charge/discharge integrals. Charging current is not automatically added to available capacity because NiMH charge efficiency has not been measured.

A qualified model requires three qualified cycles, measured current and gauge-error bounds, a qualified temperature domain and stable physical battery/calibration identities. Its lower estimate includes current and gauge-change errors and the lower envelope of the retained results. This is a conservative engineering estimate within the calibration conditions, with no statistical confidence or docking guarantee.

Integrated remaining charge starts only from a physically qualified full reference: stable native maintenance state 3, reported charge equal to capacity, fresh readings and stationary evidence. Reboot, gaps, out-of-domain temperature, gauge changes or partial charging require a new reference. Until then the API exposes OI remaining charge with its source and blocks learned reserve. Persisted history does not turn consumption while the controller was off into measured consumption.

## Return reserve

Return consumption is measured from an observed return event through confirmed Home Base contact and stable charging. The distance reference is a route through safe map cells rather than straight-line distance. Computing it neither changes the active navigation path nor authorizes movement.

Reserve requires a qualified capacity model and continuous full anchor, three accepted return trips in the same dock calibration frame, a distance inside that frame's observed domain and a measured extra margin. It takes the envelope of observed return consumption plus current error and margin. Shorter trips receive no automatic discount; scaling upward is possible inside the observed domain, with no extrapolation beyond its longest trip. Missing routes or unreliable localization block the reserve. Localization loss or a safety hazard during return rejects that trip; critical voltage also excludes a capacity fit from qualification. Changed surroundings, friction and battery condition still need renewed validation.

When conservative remaining charge reaches the reserve, an explicitly enabled adaptive setting can request an earlier return through PowerManagement. Status reports `learned_reserve`. It can only advance return. The agreed 13.8/13.5/13.0/12.5V trial thresholds remain active. No GPIO, resistor or EN cutoff setting is adjusted; the calculated approximately 12.1V hardware cutoff remains independent. Native iRobot fallback still requires explicit user approval and all safety checks.

## Persistence and API

LittleFS alternates `/battery-a` and `/battery-b` snapshots with schema/size, sequence and CRC32. Each write is read back and verified; a torn write leaves the previous slot available. Unreadable-only history or a different battery/calibration profile blocks automatic overwrite. This binary format is local to its schema; JSON is the portable representation.

Checkpoints are at least five minutes apart and only while already stopped in Passive, with fresh voltage above 13.8V and the measured Flash-write floor, no moving mission/native docking, and storage/power qualification enabled. No motion is stopped just to checkpoint. Flash latency breaks measurement continuity and requires a new full reference. A power failure can lose everything since the last safe checkpoint, including a whole moving mission. Two slots protect the snapshot, not the filesystem against every possible power failure.

All APIs use existing Bearer authentication:

| Request | Result |
|---|---|
| `GET /battery` | Estimate source/quality, remaining charge, capacity, reserve, totals and persistence state. Unavailable estimates use `null`. |
| `GET /status` | Also includes `battery_learning`; `null` if the bounded lock could not be acquired. |
| `GET /battery/history?after=0` | Up to eight cycle summaries, oldest first. |
| `GET /battery/samples?after=0` | Up to sixteen traces, oldest first. |

For subsequent pages send `after=next&lineage=...`. Archive `pack_key`, `calibration_key`, `lineage` and record ID. `boot` plus `ms` identifies an MCU clock, not UTC. `has_more` signals another page and `lost_records` reports ring overwrite. A changed history scope returns 409. Export pages to a computer before the rings overwrite them for long-term retention; firmware does not upload to a cloud service.

Quality bitmask: 1 gap, 2 gauge anomaly/partial charging, 4 unverified measurement or out-of-domain temperature, 8 reboot, 16 invalid telemetry, 32 critical voltage reached, 64 return frame change, 128 charging fault. Also inspect `capacity_accepted`, `capacity_qualified` and `return_accepted`; a bitmask alone does not qualify a model.

An explicit battery/calibration replacement or storage repair can use `POST /command`: `action=battery-reset`, `confirm=reset-battery-learning`, current `boot_id` and `lineage`, and MCU-clock `expires` at most five seconds ahead. It deletes only the two battery snapshots, requires the same stationary safe-power gate and starts no motion. HTTP 202 means queued; inspect `last_error` for the result. No reset was executed during development.

## Qualification still required

Defaults remain blocked: empty pack/calibration IDs; `ROO_BATTERY_CURRENT_VERIFIED`, `ROO_BATTERY_GAUGE_VERIFIED`, `ROO_BATTERY_FULL_REFERENCE_VERIFIED`, `ROO_BATTERY_ADAPTIVE_RETURN_ENABLED` and `ROO_BATTERY_STORAGE_VERIFIED` false. Error bounds, temperature domain and reserve margin are zero. No invented accuracy values activate the feature.

1. Identify the physical pack and its label. Use a new identity after battery replacement and a new calibration record after measurement-chain changes. Quality flags, error bounds and temperature limits are also hashed into the calibration key, isolating older results.
2. Verify current sign and error against a suitable meter under loads and charging. OI must include every RooCore load, and current return must not bypass the original shunt. Actual supply/return pads remain unresolved; this feature authorizes no new wiring.
3. Verify gauge change, native full/maintenance indication, temperature domain, Flash voltage floor and power-loss recovery physically, separately from software tests.
4. Record docking trips in the qualified corridor, including added height and docking delays; select the reserve margin from evidence. Deep discharge is not recommended for learning.

Sources: [BatteryLearning.h](../lib/RooNavigation/src/BatteryLearning.h), [BatteryLearningStore.h](../lib/RooNavigation/src/BatteryLearningStore.h), [RooCoreXiao.ino](../firmware/xiao/RooCoreXiao/RooCoreXiao.ino). Host tests and both compiler configurations are documented in the [integration record](assembly/en/FIRMWARE_INTEGRATION.md). Physical wiring and hardware operation remain unqualified.

## Manufacturer references

- [iRobot Roomba 600 OI specification](https://class.ece.iastate.edu/cpre288/resources/docs/iRobot_Roomba_600_Open_Interface_Spec.pdf), Sensor Packets: 21 charging state, 22 mV, 23 signed mA, 24 temperature, 25 remaining mAh, 26 reported capacity, 34 charging sources and 35 OI mode. The iRobot-authored document is hosted by a university; each packet's actual support on this 620 remains separately qualified.
- [Energizer NiMH Handbook](https://data.energizer.com/wp-content/uploads/2020/11/nimhhandbook_ver2-2.pdf), discharge and charging characteristics: load/temperature-dependent voltage, flat discharge profile and condition-dependent charging efficiency. This is general chemistry guidance, not characterization of the user's replacement pack.
