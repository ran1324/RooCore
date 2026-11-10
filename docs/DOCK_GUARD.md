# Home Base geometry guard

2026-11-10 · Software implementation and host tests · [עברית](DOCK_GUARD-he.md)

`lib/RooNavigation/src/DockGuard.h` is a bounded, allocation-free geometric gate for the base and exposed cables. It does not operate hardware or replace the existing cliff, bumper, floor, LiDAR, power, charging-policy or command-ownership checks. The controller must always be able to stop without asking this gate for permission. Host tests use synthetic dimensions and timestamps; no physical base size, stopping margin or Roomba 620 docking behavior was measured.

## Geometry and qualification

`Config` contains no enabled physical defaults. Set every dimension and applicable bound from commissioning evidence. `configured` and `verified` must both be true, `anchorRevision` must be nonzero, and all dimensions, map bounds and deadlines must be finite/consistent. `simulationOnly=true` configurations require an explicit `configure(config, true)` call; the firmware integration must leave this opt-in false. These flags label provenance; software cannot determine whether somebody actually performed the stated measurement.

The base is an oriented rectangle covering the entire base **and exposed cable/adapter footprint**. `frontX/frontY` identifies the front contact-face centre; `yaw` is its outward normal. In this frame the rectangle is `[-depth,0] × [-halfWidth,+halfWidth]`. If one rectangle cannot cover the actual assembly while supporting a safe approach, that assembly is unqualified for this model. Do not trim cables out of the footprint merely to permit a narrower route.

`bodyRadius` bounds the robot and fixed attachments. The side brush is a disk at robot-frame offset `sideX/sideY` with `sideRadius`; this envelope remains active even with its motor stopped because bristles can still snag. The main brush must fit inside the body bound. `margin` must include conservative measured geometry/localization error, command and observation latency, braking travel and planner discretization. It is not inferred from the broad IR force field.

`map` is the usable map extent. The guard keeps the swept body plus its margin inside this extent even during a docking exception. Deliberate bristle contact with a wall belongs to the existing edge controller, so the side-brush disk does not shrink the map boundary. It still enlarges the base/cable exclusion.

`blocksOrdinaryCenter(x,y)` expands the base by `max(bodyRadius, hypot(sideX,sideY)+sideRadius) + margin`, covering every eventual heading. For a grid cell the planner uses `blocksOrdinaryCell(x,y,halfSize)`, which further expands the base by the half-diagonal `sqrt(2)*halfSize` and the map edge by `halfSize` per axis. This covers the entire cell; checking only its four corners could miss an intrusion between corners along a rounded boundary. `blocksOrdinaryPose(p)` uses the actual orientation. Invalid configuration or invalid anchor blocks motion/planning authorization; it does not disable read-only telemetry.

## Final gate and bounded sweep

Call `permitsMotion(observation, linear, angular, horizonMs, brushesActive)` immediately before **every** direct-drive write, including local planning, leased remote commands, recovery reverse, edge cleaning, touch-up and any future airflow maneuver. The observation carries independent pose, safety and IR sample times. Pose/localization and safety freshness are required; unknown configuration, NaN/Inf, stale timestamps and impossible horizons fail closed.

The differential-drive centre follows its constant-speed arc over the authorized command horizon. Subdivision count is bounded to 256 intervals; a command requiring more intervals is rejected. Each interval expands the body disk by `abs(linear) × interval_seconds` and the offset-brush disk by that amount plus `hypot(sideX,sideY) × abs(angular) × interval_seconds`. These are arc-length upper bounds, so checking only endpoints cannot tunnel through the base. The larger the numerical step, the more conservative the result; `stepMeters` and `stepRadians` are computational settings, not hardware calibration. `maxHorizonMs` must cover the actual lifetime of the command, with braking/latency separately included in the measured margin.

## Local docking permit

Only local mission/charge supervision may call the access methods; neither a grid waypoint nor a server motion command grants an exception. The corridor is an approved **robot-centre** region after accounting for actual body/add-on/brush clearance and stopping uncertainty. Its local x range is `corridorMinX..corridorMaxX` and y range `±corridorHalfWidth`; `corridorMinX >= 0` prohibits robot centres behind the contact face. Heading must remain within `headingTolerance` of inward (`yaw + pi`). A measured corridor must not admit side or rear approach.

1. `beginApproach(observation)` requires clear front staging, fresh supported dock IR, fresh pose/safety observations and a qualified anchor. Already intersecting the ordinary base exclusion is not a valid starting point.
2. `permitsNativeDock(observation)` supervises an opaque native Seek Dock operation against the current corridor, map, pose/safety/IR freshness and `maxAccessMs`. Leaving the envelope requires the caller to stop native motion immediately. This observation-based gate cannot prove the native routine's path between reports; its sampling/stopping margin and actual 620 behavior remain commissioning requirements.
3. Direct approach motion must be forward and direct departure motion reverse while facing inward; cleaning motors must be off. Every swept interval, including turns, must remain within the corridor. Repeating `beginApproach` or `beginDeparture` does not extend an existing deadline.
4. Fresh expected contact during Approach changes the guard to `Docked`, where motion is held. The body must intersect the mapped front contact region; contact at the distant staging end invalidates the anchor. This recognizes geometry/contact only; the charge supervisor must separately debounce contact and assess actual charger state, freshness, pack policy and departure authorization.
5. `confirmDocked` is available for an explicitly qualified local anchor action while already on the base. Do not call it automatically in response to any source bit. Unexpected contact during Ordinary mode invalidates the anchor instead.
6. `beginDeparture` requires that known Docked state and fresh contact. The charge supervisor must first satisfy every independent readiness check. Call `endAccess` after contact loss and a clear return to ordinary space. `endAccess` preserves a stationary Docked hold.

An aborted permit does not authorize a retry or an alternate route through the base. The mission supervisor owns attempt budgets, stop confirmation, charger diagnosis and recovery. Corridor endpoints need clearance for the conservative interval sweep; a pose exactly at an endpoint cannot authorize a command that may extend beyond it.

## IR and moved-base evidence

`classifyIR` accepts exact Roomba 600-family characters from the iRobot-authored [OI reference, 2021-04-13, printed pp. 25–26](https://cdn.hackaday.io/files/1835247851890816/2021-04-13_iRobot_Roomba_600_Open_Interface_Spec-3.pdf): `161`, `164`, `165`, `168`, `169`, `172`, `173`. `162` is Virtual Wall, not Home Base. Discovery-family values do not count as 600 dock evidence. Actual receiver/base compatibility on this 620 is unverified.

Fresh IR corroborates approach; it supplies neither range nor pose. During an already authorized approach, a fresh zero code does not relocate/remove the base. Stale IR stops native supervision. No code widens the corridor or erases ordinary protection. Native avoidance is not assumed to protect custom Safe-mode driving.

`observe` invalidates the anchor on unexpected fresh powered contact, a near-base bumper observation, or independently supplied `anchorMismatch`. `invalidateAt` may additionally retain an independently localized suspected front face; raw IR must never be converted into such a position. Invalidation blocks all further motion and preserves the old footprint/configuration. A new explicit, qualified configuration/anchor revision is needed to recover; no map origin is silently shifted. Raw IR alone cannot diagnose that the base moved.

## Validation record

`tests/dock_guard_test.cpp` passed C++11 compilation with `-Wall -Wextra -Werror -pedantic`, AddressSanitizer and UndefinedBehaviorSanitizer. It covers unconfigured and simulation-only gates; missing/invalid/NaN calibration; rotated geometry; side, reverse and diagonal crossings; whole-path tunneling; full-turn side-brush sweeps; stopped bristles; margin inflation; orientation-independent planner inflation; ordinary clear space near a broad beacon; deliberate wall-bristle contact; approach/departure direction, heading, corridor and brush limits; native contact hold; stale/invalid pose, safety and IR data; unexpected contact and moved-anchor hold; old/suspected footprint preservation; expiry and timer rollover; subdivision budget; map boundary enforcement; and all 256 possible IR byte values.

The host model does not establish actual braking distance, cable placement, sensor timing, base dimensions, Safe departure, native Seek Dock behavior or physical absence of base displacement. The unresolved electrical and power requirements remain in [the charge/resume plan](CHARGE_RESUME_PLAN.md), [current inventory](../HARDWARE_INVENTORY.md), [electrical review](assembly/ELECTRICAL_REVIEW.md) and [LiDAR power sequence](assembly/LIDAR_POWER_UART.md). No wiring was changed by this guard.
