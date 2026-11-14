# RooCore firmware power management and docking handoff

14 November 2026. Target: Roomba 620 with plain XIAO ESP32S3. The policy is integrated into the full firmware, with hardware activation disabled until qualification. No firmware was flashed and no robot was operated. [Hebrew](POWER_MANAGEMENT-he.md) · [Electrical plan](assembly/en/POWER_DOCKING_PLAN.md) · [Build and integration evidence](assembly/en/FIRMWARE_INTEGRATION.md).

## Trial policy

| Loaded voltage | Local behavior |
|---|---|
| ≤13.8 V | Warning; existing cleaning may continue, but new missions, cleaning resume and remote takeover are blocked. |
| ≤13.5 V for 5 continuous seconds | Latch return-to-dock, invalidate remote control and switch off vacuum and both brushes. Keep LiDAR and navigation active. |
| ≤13.0 V | Immediate urgent return without waiting for the 5-second window. No cleaning, exploration or provisional server-assisted navigation. A failed route stops. |
| ≤12.5 V | Send stop in the same control tick. After fresh stop confirmation, shut LiDAR down with UART-before-power sequencing and offer native handoff. |
| About 12.1 V at regulator VIN | Independent hardware EN cutoff through the 11 kΩ divider. Firmware neither delays it nor drives D10. |

These are the agreed **trial values**. OI packet 22 supplies voltage. Cached generations do not extend the low-voltage window; stale/missing data reset that window and hold active motion. Voltage rebound never cancels return or critical hold. The XIAO navigator uses this external voltage policy instead of its old 25%/6% scheduling/stopping thresholds. Dock departure additionally requires fresh voltage above 13.8 V. Generic host navigation simulations retain their percentage policy by default; battery validity and all other safety checks remain required.

## Approval and local safety

There is no compiled or default user approval. An explicit decision can preapprove the current mission or approve an active offer. It is bound to `boot_id`, `mission_id`, and the current approval revision, and is discarded at reboot. Stale, expired, wrong-scope and replayed requests are rejected.

Every planner `SeekDock` request, including the final approach before 12.5 V, goes through the same stopped handoff gate. No alternate firmware path sends `143` without approval. After the stop command, fresh coherent OI samples must show Safe or Passive and unchanged encoders for at least one second and five distinct samples. This is a software acceptance predicate requiring physical validation, not a UART acknowledgement or proof of physical stop. LiDAR then switches off. Without approval the robot remains stopped in Safe. A fresh charging source can lead to qualified release to Passive for charging.

Approval never overrides safety. A separate verified native-dock/UART interface and a physically surveyed clearance profile for the complete add-on height and envelope are required. Runtime checks require anchored localization, fresh encoders/pose/safety/charging telemetry, no cliff, wheel drop, bumper, overcurrent, charger fault or known nearby contact hazard, plus DockGuard's mapped approach corridor and IR evidence. **New height sensors are not integrated.** Therefore this implementation does not permit unrestricted native searching through the home: fallback remains limited to a dock corridor physically qualified for the additions. Both verification flags default false.

A failed safety check consumes approval and stays stopped; removing the hazard does not silently restart motion. After sending `143`, at most one attempt is made per mission, including a partial/failed UART write. Native Seek Dock enters Passive itself. The controller continues watching OI safety, odometry and the corridor without LiDAR; loss of data, corridor violation, revocation, or the existing 60-second native-dock budget sends a stop. Charging state/source packets 21/34 are checked separately. A low-voltage return never restarts cleaning automatically after charge.

The separately added passive native-recording mode intentionally leaves native cleaning with iRobot and suspends RooCore motion/power management. This document governs RooCore-managed missions; recording mode is not an automatic low-voltage handoff.

## Firmware API

Use the existing authenticated `POST /command`. HTTP 202 means queued; inspect `GET /status` for the actual result. Only the control task touches robot hardware.

| action | Extra form fields | Meaning |
|---|---|---|
| `fallback-approve` | `boot_id`, `mission_id`, `revision`, `expires`, `confirm=native-clearance-risk-accepted` | Explicit user decision for this mission. Use `fallback_approval_revision`; `expires` is MCU milliseconds (`now` in status), at most 5 seconds ahead when executed. |
| `fallback-revoke` | None | Revoke permission; active native motion is stopped. |
| `pause` | None | Stop, including during mission arming, approval wait or native handoff. |
| `power-reset` | None | Clear the power hold after stable qualified charging on the base. Never starts a mission. |

Clients must send approval **only following an explicit user decision**, never automatically because voltage fell. The API request's 5-second freshness window is separate from accepted mission preapproval, which lasts until consumed or revoked.

Status includes `power_state`, `power_reason`, the four voltage thresholds and `power_return_ms`, `power_stop_confirmed`, `fallback_offered`, `fallback_approved`, `fallback_qualified`, `fallback_scope`, `fallback_approval_revision`, `fallback_attempts`, and measured configuration fields. `fallback_qualified` describes static verification flags only; it is neither permission nor a current safety result. `power_d10_assertions` is always zero. `power_hw_cutoff_nominal_mv=12110` is a design calculation, not a measurement.

## Activation and persistence

`ROO_POWER_POLICY_VERIFIED`, `ROO_POWER_PASSIVE_STOP_VERIFIED`, `ROO_NATIVE_FALLBACK_VERIFIED`, and `ROO_FALLBACK_CLEARANCE_VERIFIED` all default false. `ROO_POWER_HW_OFF_MAX_MV`, `ROO_POWER_RECOVER_MV`, and `ROO_POWER_SAVE_MIN_MV` default zero. Existing commissioning, encoder, LiDAR and dock qualifications also remain necessary. Old `ROO_POWER_CUTOFF_VERIFIED` configurations do not activate the new policy.

Valid configuration requires `hardwareOffMax < saveMin ≤12500 <13000 <13500 <13800 <recover`. This ordering alone does not prove enough stop/write margin; voltage error, wiring drop and real falling-voltage behavior must be measured. There is no approved real-robot Flash-write floor or recovery voltage yet.

Before a mission moves, firmware writes and reads back a marker at `/power-cutoff` (backward-compatible filename). Partial/failed persistence holds motion. Buffered UART data are invalidated, the stream restarts and a fresh confirmation period precedes movement. Pause during arming remains latched. Reboot with the marker blocks movement and does not restore approval. Explicit `power-reset` can clear it only after fresh Home Base source, charging state 2/3, Passive mode, stationary encoders and voltage above the calibrated recovery threshold remain valid for 5 seconds following stop confirmation. A new explicit mission command is then required. Storage initialization cannot erase an active marker.

Implementation: [PowerManagement.h](../lib/RooNavigation/src/PowerManagement.h), [RooCoreXiao.ino](../firmware/xiao/RooCoreXiao/RooCoreXiao.ino). The old D10 path is not called and GPIO9 is not configured as an output. Hardware EN behavior remains in the electrical plan.

`tests/power_management_test.cpp` covers threshold/time boundaries, rebound, duplicate/stale data, approval scope/replay/expiry, preapproval and event approval, safety vetoes, revocation, unqualified clearance, moving encoders, stop/storage/Dock failure, reboot persistence, charging, pause during arming and millis rollover. Real OI library transport checks verify Safe→zero wheels→zero cleaning motors, with Passive only when explicitly selected. Host results and both XIAO builds are recorded in the integration evidence; enabled build-fixture values are synthetic and must not be flashed as calibration.


Battery capacity/reserve learning can request earlier return only. It changes no voltage threshold, EN behavior or fallback approval. See [battery monitoring and learning](BATTERY_LEARNING.md).
