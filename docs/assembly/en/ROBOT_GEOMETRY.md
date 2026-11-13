# Robot and brush geometry — user-reported measurements

2026-11-12 · Roomba 620 · [Hebrew](../ROBOT_GEOMETRY.md)

Evidence consists of five initial photographs, two follow-up annotated photographs and the user's clarifications. Dimensions are approximate; no independent measurement, instrument accuracy or repeated readings were supplied. An annotated photograph does not calibrate floor contact. The dimensions are integrated after reproducing and fixing the navigation failure described below. Physical calibration remains unqualified.

| Quantity | User report | Model use |
|---|---|---|
| Main-brush region width, left to right across the robot | About 16 cm / 160 mm | `brushWidth=0.16` metres; agrees with the previous provisional width |
| Main-brush region depth, front to rear along travel | About 7 cm / 70 mm | `brushDepth=0.07` metres in code, simulation and display; replaces 40 mm |
| Body diameter | About 33 cm / 330 mm; photographs show both 32 and 33 cm | Recorded as reported. Collision radius remains 170 mm (340 mm envelope) pending verification of the outer diameter and fixed protrusions |
| Side-brush arm in the photograph | About 6 cm / 60 mm **from the start of the arm to the bristle tip**, as clarified by the user | Neither the rotation radius nor the protrusion beyond the body |
| Side-brush radius — follow-up report | About 7 cm / 70 mm **from the screw centre to the bristle tip** | `sideRadius=0.07` metres in code; calculated sweep diameter 140 mm, replacing the assumed 100 mm radius |
| Side-brush protrusion beyond the body edge — follow-up report | Up to about 5 cm / 50 mm outward | Recorded as reported reach, not main-brush cleaning width or evidence of effective floor contact |
| Rectangle in the follow-up photograph | 17×8 cm, labelled area 136 cm² | Boundaries appear to include framing; equivalence to the 16×7 region is unverified. Kept separately, not substituted for brush dimensions |
| Wheels relative to the brushes | User describes them at the start of the brush rectangle | If the wheel **centres** align with the leading edge of a 7 cm deep rectangle, its centre is 3.5 cm behind the axle: `brushOffset=-0.035` metres. This estimate is used in code; conditional calculation, not a direct measurement |

Existing lane spacing is 100 mm; a 160 mm width gives 60 mm nominal lateral overlap. This is geometry, not a dirt-removal test. The depth bounds the pair of brushes; it does not establish that every point in the rectangle contacts the floor. The follow-up photograph also labels 24 cm between points on the wheels and 18 cm with a cropped endpoint; neither establishes calibrated track width or front-to-axle distance. Odometry settings were not changed.

## Software integration status — fixed

The initial failure was reproduced with 70 mm depth, 70 mm side radius and zero main offset. At approximately `(3.344, 0.436)` metres near a furniture corner, every replan first required the robot to recenter in its current cell at `(3.35, 0.45)`. The clearance check blocked that movement toward the corner. Selecting another goal still returned to the same initial waypoint. After eight skips without progress, it stopped with **`NoProgress`**, not the previously reported `NoRoute`. This scenario did not involve a collision.

The fix removes the start-cell centre from the waypoint list when the route has another cell, while retaining a same-cell goal. Planning begins at the actual pose. Clearance checks, wait/retry limits, floor hazards and map boundaries remain in force. Cleaning targets now also account for the brush offset: the planner selects a free body position that places the brush centre over the floor sample, checks the body cell and aligns the final heading when needed. Reaching a point with the body does not itself earn cleaned credit.

Active defaults in the navigation library and example configuration are **160×70 mm, −35 mm main offset and 70 mm side radius**. Side pivot `(100, -130)` mm remains provisional and the collision envelope remains 340 mm. Simulation obtains geometry from the C++ core for coverage truth, side-brush contacts and rendering. `/status` and the display also carry brush depth, offset and side dimensions, so drawings match the reported robot configuration. Existing `config.local.h` files are not overwritten; update their geometry fields during later preparation without copying synthetic enable flags.

Regression tests cover the corner failure pose and rearward footprint, and retain the original scenario with zero offset. Hardware and dirt-removal acceptance remain separate from software checks. See [validation results](VALIDATION.md).

## Remaining calibration

- Rectangle centre relative to the drive-wheel axle midpoint: conditionally estimated at −35 mm from the user's wheel-position description and 70 mm depth. Code uses `brushOffset=-0.035`; it remains a report-based estimate. If the relevant depth proves to be 80 mm with the same alignment, the calculation becomes −40 mm; do not choose between them solely from the drawn rectangle.
- Side-brush screw position relative to that axle: `(100, -130)` mm remains an assumption; X is forward and Y is left. The reported 50 mm protrusion does not determine both pivot coordinates, so no X/Y position was inferred. The existing development position is not yet calibrated to that protrusion.
- The 70 mm side radius comes from the user's follow-up report, without independent verification. Three equal arms rotate about one axis; their count does not change the radius. Reported protrusion is 50 mm: do not add the full radius to the body diameter or infer effective wall cleaning from it.
- Active floor-contact width, precise body diameter and fixed additions remain unverified. `ROO_BRUSH_GEOMETRY_CALIBRATED=false` is unchanged; these dimensions do not qualify driving or docking.

The user's description supports an initial placement estimate. For precise verification, measure without operating the robot, using a physical ruler or tape from the line joining the drive-wheel **centres** to the active region's centre, recording its boundaries. Individual rollers need not be measured separately, and motors need not run. Floor cleaning performance requires a separate later test.

## Supplied chat photographs

- 33 cm diameter: `D2AC5C83-DEDF-4951-9B2F-84BEB8576C6B.jpeg`.
- 32 cm diameter: `82942D62-0B5C-4474-A372-93948A066A51.jpeg`.
- 6 cm side arm: `C5427388-8BDD-4EA7-BE32-1FD9B3C19940.jpeg`.
- 16 cm width: `C47E9E27-1541-4CF8-B07B-9BB5A17A48D7.jpeg`.
- 7 cm depth: `48D2428E-7CB5-462D-B5EE-DA2A1CE7C0A7.jpeg`.
- Wheel position and 24/18 cm lines: `730404C4-8E43-409E-9594-84D46500605C_4_5005_c.jpeg`.
- Follow-up 17×8 cm rectangle: `98AEFBCB-6DA3-4009-98D9-48294A2ACC7B_4_5005_c.jpeg`.

The photographs remain in the user's photo library and are not bundled in the portable kit. No firmware was flashed and no physical robot test was performed.
