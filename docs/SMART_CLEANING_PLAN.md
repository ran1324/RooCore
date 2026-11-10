# RooCore — live progress, automatic cleaning and maintenance plan

2026-11-10 · Accepted user requirements; implementation is staged. This document does not claim the new features are active on the robot. No flashing, motor operation or physical measurements were performed.

[Hebrew canonical plan](SMART_CLEANING_PLAN-he.md) · [Current behavior](XIAO_NAVIGATION.md) · [Coverage and edges](COVERAGE_AND_RECOVERY.md) · [Dirt collection](DIRT_DETECT.md)

## Accepted requirements

| ID | Requirement | Deliverable |
|---|---|---|
| LIVE-1 | Website progress from robot reports | Map, estimated pose, coverage, state, battery, pass, stop reason and report age; explicit real/simulated source |
| MAP-1 | Reusable saved map and local copy | Separate house map and mission progress, versioning, localization and consistent restore |
| AUTO-1 | Extra pass based on dirt and history | Explained, bounded decision using valid calibrated evidence; elapsed time is not proof of dirt |
| CARE-1 | Service/replacement dates and usage recommendations | Component history, usage counters, sourced iRobot reminders and missing-data states |
| EDGE-1 | Attempt accessible wall edges/corners and sweep with side brush | Uncovered-segment tracking, bounded maneuver and central-brush collection pass |
| AIR-1 | Aim existing rear exhaust at a corner | Feasibility trial and collection comparison, not yet an accepted autonomous hardware behavior |
| BATT-1 | Detect shortening runtime and repeated charging | Comparable mission analysis, abrupt change/trend and other return causes separated |
| CHARGE-1 | Return, charge and resume without server dependence | Local state machine, correct power sequence and preserved interruption point |
| DOCK-1 | Clean near the base without climbing onto or moving it | IR plus protected mapped footprint and state-gated approach/departure corridor |

These are accepted product requirements, not a second request for planning approval. New physical motion remains subject to the existing commissioning checks and feature-specific verification.

## Implementation status — 2026-11-10

- LIVE-1: implemented authenticated coherent firmware `/progress`, read-only server adapter/replay, live canvas, freshness and source labels, SQLite recording and mission history. Server-held identities and credentials are configured explicitly. See [dashboard setup](DASHBOARD.md).
- MAP-1: firmware new missions retain occupancy and reset mission coverage; explicit `map-clear`, versioned local snapshots, map/mission/boot identities and boot reanchoring. This is a single reusable local map, not yet a named multi-map/room editor.
- CARE-1 / BATT-1: implemented component installation/clean/replacement/snooze events, iRobot date guidance, observed usage counters with gaps and advisory comparable-runtime/recharge analysis. Unknown dates and missing policy remain unknown. Simulation/replay never generate real battery alerts from simulated samples.
- CHARGE-1 / DOCK-1: implemented local charger-state policy, stable contact/readiness, latched LiDAR preparation, active-time accounting, explicit return causes, bounded dock geometry/corridors and final swept-motion checks. Automatic BRC wake is held pending verified hardware/behavior. See [charge implementation](CHARGE_RESUME_PLAN.md) and [dock guard](DOCK_GUARD.md).
- AUTO-1 / EDGE-1: still pending the bounded automatic-pass policy and full segment accounting. Existing `DirtMonitor` remains observation-only; existing perimeter/touchup motion remains, with new dock protection applied. Attempted edges are not proof of complete coverage.
- AIR-1: physical feasibility and collection experiment remains pending. Simulation does not model airflow or establish corner dust removal.

The website and firmware contracts have been tested with synthetic telemetry and host physics. No hardware flashing, robot operation or physical calibration has taken place.

## Live website and history

The server stores history and serves authenticated robot reports to the website. The MCU retains motion arbitration, sensor checks and expiring command ownership. Robot credentials remain on the server. Opening a page never starts cleaning.

A coherent progress snapshot needs `robot_id`, `boot_id`, `mission_id`, `map_id`, `map_revision`, coordinate frame, sequence, measurement time and receipt time. Map, pose and coverage must share a revision. Add a bounded, coherent snapshot/tile endpoint without blocking the control task; `/progress` is implemented using fixed-size PSRAM snapshots, serialized outside the control loop.

Display central-brush passage, side-only sweep, observed-but-uncovered floor, unknown floor, failed targets and inaccessible areas separately. Percentage of known floor is not percentage of the whole home. Lost connection or localization freezes the last reliable marker with an explicit warning/age; do not invent motion or connect traces across pose jumps.

Persist separately:

- Start/activation time, active cleaning time, pauses and charging intervals.
- Last reported brush passage per region in a valid map frame, with actual covered extent.
- Program completion, coverage, missing perimeter sections and finish reason. `Complete` alone never establishes whole-house cleanliness.
- User-reported manual cleaning, scoped to time and region and identified as a user report.

Map/frame changes cannot copy history to new coordinates without verified alignment. Missing telemetry is not work time or a successful mission. Store dates in UTC, display Asia/Jerusalem, and measure operating intervals with a monotonic clock.

## Automatic decision algorithm

The user selects automatic mode for a mission. Autonomous departure solely because time elapsed is outside this initial scope and requires a separate scheduling preference.

1. Validate fresh sensors, localization, dirt calibration and comparable cleaning conditions. Missing evidence means unknown, never clean.
2. Run a baseline pass, associating readings with regions. Require sustained readings and sufficient sampled time/area, not one spike; compare only calibrated PWM/motion/surface conditions.
3. Prioritize using actual last passage and historical dirt. A user-selected cleaning interval is a maintenance preference, not measured dirt. Partial or incomparable sessions do not train an accumulation model.
4. Consistent local dirt supports a local revisit; evidence across many regions may justify a broader extra pass. Derive thresholds from labeled recordings rather than an arbitrary raw value such as 90.
5. Initial product limit: one automatic extra pass per region per mission. Configure an overall time/energy budget inside existing navigation limits. Extra cleaning cannot reset recovery or failure budgets.
6. If comparable readings do not improve, stop repeated attempts and recommend brush/filter inspection or inaccessible-area review. Lower DirtDetect alone is not proof of cleanliness.

Record the region, evidence, policy version, history provenance, reason, confidence, proposed action, accepted action and outcome. Battery, safety, stale data and localization take precedence. Any future revisit command must carry mission/frame identity and expiry and be accepted by the MCU; reconnect cannot replay an old action.

## Maintenance and parts

Source checked: [iRobot legacy-series care table](https://support.irobot.co.uk/articles/en_GB/Knowledge/11179), not the newer Plus 600 family.

| Part | Clean | Replace per table |
|---|---|---|
| Bin | After use | Unspecified |
| Filter | Weekly; twice with pets | 2 months |
| Main brushes | Weekly; twice with pets | 6–12 months |
| Front caster | Fortnightly | 12 months |
| Side brush | Monthly | Unspecified |
| Cliff sensors/contacts | Monthly | Unspecified |

The [600-series care instructions](https://homesupport.irobot.com/articles/en_US/Knowledge/8247) prohibit washing the filter and include brush-bearing care. Link the original instructions from the UI. Do not transfer one part's replacement interval to another.

Each part stores type/identity when known, installation/replacement date, last service, reporting source and counter baselines at replacement. Cleaning does not reset replacement age. Unknown dates remain unknown; purchase is not installation.

Support separate cleaned/replaced actions, date correction and reminder snoozing with retained history. Count missions and commanded brush/vacuum operating time only during valid observations; command time is not a measured mechanical revolution count. Telemetry gaps explicitly reduce data coverage.

Show age guidance alongside usage and faults. Do not infer a physical wear percentage from age. Any user-defined hour threshold is labeled RooCore/user policy, not an iRobot specification. Usage-based acceleration produces an explained early-inspection recommendation.

## Walls, corners and sweeping

Attempt each accessible floor/wall segment and corner within finite budgets. Track detected segment, attempted maneuver, side-brush footprint, central collection passage and inaccessible remainder separately. `edgeSeen` is not a cleanliness flag.

Proposed maneuver: safe staging, controlled rotation/arc placing the side brush near the edge, then central-brush collection over the adjacent inner strip. Differential-drive motion uses turns/arcs, not lateral translation. The bumper is not a sweeping tool.

Measure real brush reach and robot envelope. Rotation does not extend bristle reach into a geometrically inaccessible corner. Side-only floor cannot receive central collection credit. Any retreat needs the system's verified rear floor corridor. Dock exclusion applies to all these maneuvers.

### Rear exhaust experiment AIR-1

The user reports a noticeable rear air jet on their 620. This is user-observed physical evidence, not an agent measurement. Investigate orienting that existing outlet toward a corner without adding a blower or altering the robot body.

The 2021 iRobot OI document, based on the 600 platform, defines independent vacuum PWM 0–127 with one motor direction; it has no jet-direction command. See [page 15, Opcode 144](https://cdn.hackaday.io/files/1835247851890816/2021-04-13_iRobot_Roomba_600_Open_Interface_Spec-3.pdf#page=15). Actual 620 response still requires verification.

Conditional sequence: stable corner detection → collision-free pose aiming the rear outlet → bounded supported vacuum operation → rotate and collect over the possible debris landing area. A front bumper cannot authorize blind rear approach. Do not obstruct or constrict the exhaust.

Compare an ordinary pass against this maneuver with matched geometry and documented conditions: corner residue, collected material and material scattered beyond the collection area. An airborne dust cloud is not success. Check for harmful dispersal, cooling interference and abnormal load. Angle, clearance, power and duration remain uncalibrated; no physical operating values were selected in this discussion.

## Battery, charging and dock protection

Battery health distinguishes battery-driven return from manual home, faults and localization loss. About one hour is a reference expectation, not a fault cutoff with add-on loads and a different cleaning profile. Compare similar start/end charge and track battery replacement; one short run does not establish degradation.

The original `goHome()` ambiguity is fixed with an explicit mission/boot-bound return-cause event. Percentage or state alone is still not evidence of a battery-driven return. The mission clock now excludes charging and pauses; this behavior is covered by long-dwell host tests.

The user describes a nickel battery; exact chemistry/model still requires its label. Do not apply a lithium 80% longevity heuristic. Mission departure threshold differs from charge termination: the original charger manages charging. The [charging review](CHARGE_RESUME_PLAN.md) identifies actual controls and code gaps.

The user reports effective but overly distant native IR avoidance. Combine dock IR codes with mapped protected base geometry and a state-gated approach/departure corridor. IR is not a centimeter range sensor. Derive clearance from measured dimensions and validation, not guessed reductions. All cleaning, sweep and air maneuvers obey the same exclusion. A moved base invalidates automatic reliance on its old anchor.

## Implementation sequence and acceptance

| Stage | Work | Acceptance |
|---|---|---|
| P1 | Telemetry contract, durable history and live website adapter | Coherent report-derived map/coverage, provenance/gaps/disconnect shown; page load never drives |
| P2 | Separate saved house map and mission progress; local cache | New mission preserves structure and resets progress only; boot does not trust saved pose |
| P3 | Parts history/reminders and battery analytics | Separate service/replacement, no double counters, visible missing evidence and sources |
| P4 | Charge/resume fixes and dock protection | Cleaning-charge-resume survives; charging does not consume motion budget; no ordinary traversal of base |
| P5 | Auto decisions and segment/sweep handling in simulation | Bounded explained revisits, honest coverage and no unbounded retries |
| P6 | Robot calibration/verification; separate AIR-1 trial | Recorded measurements, collection comparison and correct stops on sensing/communication failure |

Software cases: duplicate/late reports, reboot mid-mission, clock jumps, disconnects, distinct home causes, changed map/frame, partial history, unknown replacement date, labeled clean/dirty traces, concave/convex corners, doorways and a base below scan height. Coverage simulation does not model dust or airflow physics.

Track implementation, software tests, deployment and physical acceptance separately. This plan changes no wiring or assembly instructions.
