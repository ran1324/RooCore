# Coverage, edges and navigation assistance

Implemented for plain XIAO ESP32S3. Software validation is not physical acceptance; no robot was connected or flashed.

## Geometry and what “cleaned” means

The [iRobot 620/630 specification](https://www.irobot-jp.com/press/pdf/20120911.pdf) gives a **340 mm diameter and 92 mm height**. It does not give active brush width. The user approved a provisional **160 mm central swath** until measurement. This is 47% of body diameter, not a claim about cleaning efficiency. Provisional depth is 40 mm, centred on the axle; right side-brush centre is `(100, -130)` mm with a 100 mm sweep radius. All brush dimensions/offsets must be measured and can be changed in `config.local.h`.

- The navigation grid is 100 mm; coverage has four 50 mm floor samples per cell and independent masks for two passes. Only samples inside the **oriented moving central-brush rectangle**, while its motor is commanded and observations are valid, receive main-brush credit. Bounded interpolation fills the motion between observations; a stopped motor, stale pose or pose jump cannot draw a fictitious joining ribbon.
- Side-brush sweep is stored separately. Yellow is side-only; green is central-brush coverage. Neither establishes that dirt was actually removed. Rendering a 50 mm sample as a square is a discretized estimate, not millimetre precision.
- Actual lane spacing is 100 mm with the provisional 160 mm brush, giving 60 mm nominal overlap. Configuration rejects spacing wider than brush width minus the minimum overlap.
- Daily uses horizontal lanes. Deep adds vertical lanes, beginning from the current position, with a separate coverage mask. The planner routes around obstacles and fills individual uncovered samples; it is not restricted to straight stripes. Narrow connected zones can use short local paths. After perimeter work, up to 32 remaining samples per zone can receive a short approach and bounded stationary rotation with the right brush; this handles reachable small patches that cannot fit a whole stripe. It does not shrink the robot footprint.
- Geometry/coverage is independent of body collision checks. Ordinary routing inflates observed obstacles/unknown space by the 170 mm radius, 30 mm margin and grid quantization allowance. A theoretically passable real gap can therefore still be rejected until finer geometry/calibration supports it.
- A complete program is **not** a claim of 100% floor coverage. Known floor, main-brush coverage, side-only coverage, pending safe samples and excluded targets are distinct. Unknown space is not free space. The simulator also has an independent 25 mm truth metric over the entire physical floor, including its margins.

## Areas, doors and walls

The local planner partitions known free space at observed narrow openings (nominal threshold 1.4 m). It finishes an area's passes and perimeter before deliberately crossing a portal. Corridors become their own zones. This is a conservative grid heuristic, **not semantic recognition of every room**: furniture can subdivide a room, and wide/angled openings can remain one zone. The MCU rejects server motion that would leave the current zone during its cleaning phase.

Up to 32 safe frontier viewpoints are visited to inspect nearby unknown space; routes never pass through unobserved cells. Unreachable/unknown floor is not silently marked clean. Failed targets and mission-wide time limits remain finite.

Perimeter following keeps the right side brush toward the wall at PWM 127 with 0.05 m/s body speed. Nominal centre-to-wall distance is 0.215 m. A corner uses a bounded quarter-turn; a missing right wall or portal causes exit back into the current area rather than following it into another room. A straight wall face is attempted once; actual brush coverage, not the attempted flag, controls floor colouring. There are 16 perimeter segments per area, a 15-minute area perimeter limit, 6-second corner turns and a 20-second exit limit. A body that cannot make safe progress stops instead of endlessly spinning.

OI packet 8 (wall) and 27 (wall signal) are sampled. `ROO_WALL_IR_TARGET=0` means telemetry only: there is no invented conversion from raw IR to centimetres. After calibration, IR adds a bounded correction to the right-wall controller; it never overrides cliff/drop or LiDAR validity. The front bumper detects contact, not distance ahead.

`ROO_WALL_TOUCH_VERIFIED` defaults to false. Its optional commissioned mode approaches a known front wall at 0.015 m/s, at most 80 mm / 6 seconds. First bumper contact stops immediately. After 200 ms it can release backward at 0.02 m/s, at most 30–40 mm / 2.2 seconds, only with verified recent floor behind it and rear clearance. At most eight contacts per mission; no repeated tapping of the same corner. Brushes stop during release. Persistent bumper, drop, cliff, current fault or expired sensor data stop the attempt. The narrowed LiDAR envelope applies only to this bounded approach, not normal driving.

The side brush may reach the floor/baseboard junction. This does not promise cleaning the vertical skirting or every sharp corner a circular body cannot reach.

## Moving obstacles and contact memory

A new scan hit blocks the cell immediately. It does not require deciding whether the object is a person, pet or furniture. If forward clearance is lost, `Yielding` stops drive and brushes for at least one second, with an eight-second maximum wait. It then replans around the obstacle when the current footprint is safe. If the body itself cannot be placed safely, it reports `NoRoute` to the existing assistance flow. Cliff, wheel drop, overcurrent, stale essential observations and critical battery still take precedence during waiting.

Occupied cells reopen only after **20 consecutive fresh scans** contribute free evidence, all four inner-cell probes have been traversed by free rays, and no hit has arrived for at least **2.5 seconds**. One tangent ray or elapsed time alone cannot erase furniture. Nearby skipped goals become eligible for one retry; unrelated goals do not reset. A goal has at most two failed attempts. Eight failures without meaningful new central coverage or 16 waits without such progress stop the local attempt; mission time remains bounded. A blocked inter-room target is cancelled before selecting another one, avoiding the stale-doorway loop.

A bumper contact is recorded approximately at the front of the robot, stored in snapshots and exposed as contact coordinates (`/status`, first 32 plus total count). The simulator draws a red cross. A **bumper-only obstacle may be below the LiDAR**, so laser rays above it cannot prove it disappeared: it remains blocked. If a laser return corroborates that occupied cell, the same repeated free-ray rule can later clear it. A nearby recorded bumper contact also prevents automatic provisional-map reset from discarding that hazard; help can still be requested while motion stays held. This is conservative occupancy evidence, not object identity tracking or guaranteed human/pet detection. Optional wall touch requires a cell observed occupied for at least ten seconds and commissioning; it is never enabled by default for new obstacles.

The browser's “מכשול שזז אחרי 20 שניות” inserts a temporary object and removes it after twenty simulated seconds. Floor beneath it remains in the cleaning denominator. The native regression checks obstacle appearance, collision-free rerouting, clearance and subsequent central-brush coverage of its former position. Low bumper-only obstacles and safety interruption during waiting have separate tests. Physical detection range, braking latency and behavior around people/pets remain unvalidated.

## Continue cleaning while requesting help

The firmware requests assistance on an unresolved local pose or safe-route failure, after stopping and saving the original mission. It publishes a request ID, coordinate frame, reason, live body-centred scan and current telemetry. An authenticated `GET /help/map` returns the frozen original map and safe-cell mask.

If fresh LiDAR, wheel telemetry and floor/safety sensors permit local motion, a **separate provisional map** is allocated in PSRAM and local cleaning starts in its own coordinate frame. The original map/coverage is not overwritten. The ROS bridge suppresses global `/scan`, `/odom` and TF publication for provisional/unresolved frames, so the new local origin cannot corrupt the old SLAM map. This also allows useful local work in a room absent from the old map; it does not pretend to know where that room belongs. If local navigation itself fails, the robot stays stopped and pending help. Two assistance episodes per mission and the two-hour local mission limit prevent repeated reset/retry loops.

The optional computer worker uses NumPy to search the known map for a scan match, with a finite search grid/time budget, free-ray checks and a margin over competing spatial/orientation hypotheses. Symmetric/unknown rooms return **no match**. Server commands contain request/frame IDs and expire in MCU time. They cannot disable safety, extend a recovery budget or take over wheel ownership. A returned pose is adjusted for intervening local odometry, then the stopped MCU requires three fresh consistent scans before accepting it. An operator pause cancels automatic motion resumption.

After accepted relocalization, the primary mission resumes. Provisional coverage remains separately recorded; it is not blindly merged into a global location. This can result in cleaning an area twice, which is preferable to false clean credit. This is a bounded global scan matcher, not a general SLAM submap/pose-graph merge. Real-world ambiguity, localization accuracy and kidnap detection still require physical testing; a pickup is not guaranteed detectable from scan similarity alone. The authenticated `relocate` action explicitly declares a lost global pose for an active or interrupted mission after moving the robot; it starts the same assistance flow without inventing a dock anchor.

At 25% charge, an ordinary mission first tries its known route home. If no route/anchor exists but local sensing is healthy, the provisional mission invokes the Roomba's native Seek Dock command as a last-resort IR search. It monitors actual dock contact and charging, with a **60-second deadline**; at critical battery (6%), safety failure or timeout it takes control back and stops. It does not search indefinitely between rooms. Native IR docking on this particular 620 is not physically validated. Charging after an unanchored search does not automatically establish a global pose or resume an old route; explicit dock reanchor or verified relocalization is required.

A server outage does not stop healthy provisional cleaning. A stale response cannot restore an old coordinate frame. Missing essential sensors, floor hazards and exhausted progress/battery budgets do stop motion.

## Run the assistance worker

On the existing computer, alongside or independently of Nav2:

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install 'numpy>=1.24,<3'
export PYTHONPATH="$PWD/ros2/roocore_bridge"
export ROOCORE_URL=http://ROBOT_IP
read -s ROOCORE_TOKEN
export ROOCORE_TOKEN
python3 -m roocore_bridge.assistance
```

For an installed ROS package: `ros2 run roocore_bridge assistance`. Credentials stay local. The current HTTP link is for the trusted LAN only. The worker acknowledges requests, tries at most eight global searches per request, and never sends raw wheel commands. `help-continue` is an acknowledgement, not a command to override a stopped robot. `help-hold` stops; `help-relocalize` proposes a pose for independent verification. A person or another server component can use the same authenticated protocol.

Snapshots use schema 4, including contact flags and per-goal retry counts. Primary files are `/mission-a`, `/mission-b`; provisional files are `/local-a`, `/local-b`. On reboot, only the primary mission is offered paused for physical reanchor; local files are retained but never implicitly merged/resumed.

## Simulation boundary

The browser now runs the **same C++ Navigator library** through a local ctypes adapter; physics, floor truth, ideal pose and charging are simulated in Python. The old Python planner remains only as an explicitly labelled historical reference for its regression tests. The browser does not execute the full ESP32 Wi-Fi/UART/task/persistence/assistance orchestration. Those paths have target-build checks and separate policy/protocol tests; they still need integration tests on hardware.

## Optional carpet estimate and vacuum boost

`CarpetBoost.h` adds a **load-based estimate**, not a dedicated carpet sensor. The iRobot OI specification exposes main-brush current (packet 56) and independent vacuum duty cycle (opcode 144, 0–127), but no carpet-entry/exit packet. Availability and useful current readings on this actual Roomba 620 still require verification. LiDAR range and cliff IR alone do not reliably identify floor material; cliff protection remains unchanged.

The feature defaults off. `ROO_CARPET_CURRENT_VERIFIED` separately enables packet 56 in the stream; verifying wheel encoders does not enable it. `/status` exposes fresh current in mA (otherwise null), battery voltage, brush/vacuum PWM and measured forward/yaw speed for calibration. After recording labelled hard-floor/carpet runs, set both current profiles (brush PWM 85 and 127), their common measured battery-voltage envelope, and `ROO_CARPET_BOOST_ENABLED`. Every profile requires positive `minimum < exit < enter < maximum` thresholds. All example thresholds are zero, so an incomplete configuration cannot boost. If floor/carpet distributions overlap, leave estimation disabled rather than choosing a misleading threshold. Repeat calibration after brush or floor changes.

Classification requires healthy sensors, forward motion verified from odometry (0.03–0.25 m/s), modest yaw rate (at most 0.25 rad/s), valid fresh current and voltage within the measured envelope. After one second of brush/PWM settling, at least eight distinct samples and 1.5 seconds of sustained high load establish `carpet_estimate`. Sustained low load for 2.5 seconds establishes `hard_estimate`. Intermediate load retains the previous estimate (hysteresis). Duplicate samples cannot advance confirmation. Missing data, implausible current, stopping, unsafe observations or uncalibrated operation return to `unknown`. A cable or jam can resemble carpet load, so current thresholds are not a reliable jam detector; native overcurrent and all navigation safety guards retain precedence.

On an eligible carpet estimate, **only vacuum PWM** rises to 127; the central brush remains at its requested PWM so the calibration baseline does not change. On a hard-floor estimate, vacuum returns to the requested profile. Deep mode already requests 127, so there is no additional boost. A zero cleaning command always stays zero; boost cannot restart paused/stopped motors. This conservative policy may temporarily drop the boost during turns or uncertain observations.

Telemetry includes `floor_estimate`, `carpet_boost_configured`, `carpet_entries`, `carpet_exits`, `carpet_entered_at_ms` and `carpet_exited_at_ms`. Times are **detection times in MCU uptime**, delayed from the physical boundary. First identifying carpet after start/resume counts as a detection; losing evidence is not reported as a carpet exit. Estimates/counters are runtime-only and never become verified floor-map labels. The browser currently does not simulate brush-current/material physics; native policy tests use explicitly synthetic readings, and the XIAO compile test exercises enabled and disabled paths. No hardware performance or detection accuracy is established, and no extra sensor is ordered.

Source: [iRobot Create 2 / Roomba 600 OI specification, pp. 15 and 32](https://marsuniversity.github.io/ece387/iRobot_Roomba_600_Open_Interface_Spec.pdf). The documented current and PWM interfaces support experimentation; they do not establish carpet recognition on a 620.
