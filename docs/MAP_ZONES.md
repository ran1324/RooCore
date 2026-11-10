# Saved maps, room labels and carpet areas

2026-11-10 · [עברית](MAP_ZONES-he.md)

The dashboard's **Home planning** editor works directly on the map. Choose a room, carpet or exclusion and draw a rectangle; virtual walls use a line. Drag a selected area to move it and drag its corners to resize. Two taps also create a rectangle. The inspector contains names and cleaning settings, with buttons and keyboard arrows as alternatives to dragging. There are no coordinate-entry fields. Undo restores draft edits; Save map commits the entire document.

## Available behavior

- Room rectangles have a name and type: living room, bedroom, kitchen, bathroom, hallway or other. They are semantic labels, not automatic room recognition or a room-only cleaning command.
- Forbidden areas can describe pet bowls, a bathroom, low furniture or any other exclusion. No-go geometry takes precedence over a carpet in the same place. A labelled bathroom alone is not a no-go area.
- Carpet settings include present/absent, maximum cleaning command and **0–2 extra passes in addition to the general program**. For example, one general pass plus one carpet pass means two on the carpet, while two general passes plus two extras means four. Overlapping carpet policies take the maximum extra count, not their sum. Absent carpets retain their editable geometry but contribute no boost or extra passes.
- On a marked, present carpet the shared planner requests main/side brush PWM 127 while cleaning (a deliberately stopped side brush remains stopped), with speed capped at the existing deep-clean speed of 0.12 m/s. The existing output adapter requests vacuum PWM from the same cleaning value. Stopped/travel commands never start cleaning merely because of the carpet.
- Additional carpet coverage has its own sample masks. Ordinary traversal during a general pass is not pre-credited as an extra pass. The UI shows the current pass and its applicable coverage; unreachable areas remain uncleaned.

## Persistence and map identity

Documents in `recordings/maps/` contain the occupancy snapshot, name, annotations and revision. Source, robot identity, map identity, grid resolution, dimensions and origin form the storage key. A different map/frame cannot silently inherit exclusions. Updates use optimistic revision checking and atomic file replacement. Invalid documents fail closed instead of silently dropping barriers. Saving is allowed while stopped; a new exclusion overlapping the current robot or dock clearance is rejected before persistence.

The editor shows saved occupancy as a visual planning background. It is **not fed back as current free-space evidence**. New scans and valid localization are still required by navigation. The simulator reloads the annotations after reset/restart; live robot and replay modes store annotations only and never forward motor commands. Draft changes are visible before saving and do not change the active simulator policy.

## Navigation boundary

`MapZones.h` and `RooNavigation.h` implement the policy in the shared C++ planner used by the simulator and built into XIAO firmware. No-go masks remain separate from observed occupancy, transient obstacles and coverage. Planning inflates exclusions by robot radius, margin and a half-cell diagonal. Every emitted drive intent also checks a swept circular footprint, including reverse and turns, over the existing 0.5 s command horizon. The 1 cm sweep step has an extra 1 cm guard. These are software/model parameters, not measured stopping margins. The chassis, accessories, uncertainty and actual stop latency still need qualification.

**Deployment limit:** the current dashboard robot connection is read-only. There is no map-zone upload/acknowledgement or persistent zone snapshot in the XIAO application yet; saved dashboard annotations do not protect a physical robot. Do not represent host saving as robot activation. A future deployment must bind policy to an acknowledged map revision, persist it alongside the mission, stop on missing/mismatched policy and preserve it across local fallback/relocalization and reboot. A stock native Seek Dock path is not bounded by this planner: with barriers loaded, the default policy holds before Seek Dock. Only the host simulator opts into its stationary docking model. This flag must not be enabled on hardware.

No wiring, power sequence, component inventory or commissioning flags changed. Nothing was flashed or operated on the robot.

## Carpet detection: what is and is not known

The existing `CarpetBoost` uses fresh main-brush current, separately calibrated bands for brush commands 85 and 127, battery-voltage bounds, startup rejection, hysteresis and sustained evidence. It remains disabled without measured calibration and confirmed packet 56 support on this exact 620. Its `carpet_estimate` is a load-based estimate, not proof of material, and it cannot predict carpet before reaching it. It boosts vacuum only; manual mapped carpet policy provides the explicitly requested main-brush increase and extra passes without feeding that PWM change back into an uncalibrated detector.

Raw cliff reflection or wheel-current changes can be retained for later experiments. They do not establish that a signal change is safe floor, distinguish carpet from a cable/jam, or justify suppressing a cliff/overcurrent stop. There is no automatic carpet polygon creation, removal or presence override from these signals. To qualify detection: confirm packet availability and units, record labelled hard-floor/carpet/threshold/cable-load samples at both PWM settings and the intended voltage range, then validate on held-out transitions, dark floor and dirty brushes. Keep stale/invalid samples unknown. No measurements or calibration thresholds were invented here.

Manufacturer reference: [iRobot Create 2 OI specification based on Roomba 600, 2016-08-10](https://marsuniversity.github.io/ece387/iRobot_Roomba_600_Open_Interface_Spec.pdf), p. 15 for command 144 and its legal −127…127 brush / 0…127 vacuum ranges; p. 29 for cliff signal packets 28–31; p. 32 for motor-current packets 54–57. These describe duty-cycle commands and signal readings, not guaranteed RPM, suction, carpet classification or confirmed capabilities of the user's 620.

## Verification

- `tests/run.sh`: existing OI, power, current-estimation and navigation cases plus `map_zones_native_test.cpp`, with ASan/UBSan.
- `tests/test_map_zones.py`: invalid geometry/policy, identity separation, stale-edit rejection, corrupt storage, dock overlap rejection, presence toggle, boost/safety priority and HTTP reset/persistence.
- Independent simulated trajectory: room with a carpet and exclusion completed both general and carpet passes, requested boost, had zero collisions, and stayed at least 0.2646 m from the forbidden rectangle. The observer checked simulator integration positions, independently of the policy geometry. This is a software result, not a physical clearance measurement.
- Browser verification covers drawing, naming, moving/resizing, undo, saving/reload and the responsive editor. Physical localization, pile climbing, brush response and stopping distance remain untested.
