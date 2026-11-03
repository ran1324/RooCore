# LiDAR and home mapping plan

Status: planned, November 4, 2026. We have not selected or bought a LiDAR sensor. Mapping and autonomous navigation are not implemented yet.

## Goal

Build a map of the house, locate the Roomba within it, and eventually plan systematic cleaning and measure coverage. We will start by recording the robot's original cleaning behavior. LiDAR scans will join those recordings before we take over motion control.

## Planned setup

```text
Roomba OI → ESP-01 → Wi-Fi → RooCore server
                                  ↑
LiDAR → companion computer/bridge ─┘
                                  ↓
                  time alignment + wheel odometry
                                  ↓
                      SLAM → map + robot pose
                                  ↓
                    future cleaning planner
```

The ESP-01 will keep handling the Roomba connection. Its serial port is already used by OI, so we cannot assume it can also handle a LiDAR sensor. We will use a separate small computer or bridge, chosen around the sensor's interface and requirements. Our server will receive both data streams and process them. Tests of communication latency and performance will determine where that processing should run in the final setup.

## Recording format

- Raw scans: scan number, start and end times, angles, distances in meters, and flags for invalid readings. Keep per-measurement timestamps if the sensor provides them.
- Wheel movement: raw encoder counts, measurement times, counter rollover and recording gaps. Encoder support on our Roomba still needs a physical test.
- Roomba sensors: bumper, cliff and other available readings, alongside the existing recording.
- Calibration: effective wheel diameter, wheel spacing, and the LiDAR's position and orientation relative to the robot's center: x, y and yaw.
- Processing results: estimated position, heading, estimate quality and map version. Keep the raw data so we can try new algorithms on the same recording.

Each source will have a device ID, boot ID, sequence number and local monotonic timestamp. We will measure clock offsets and their uncertainty. The arrival time of a Wi-Fi message alone does not tell us when the measurement happened. Disconnects and missing data will remain explicit gaps.

## Mapping and localization

We will estimate movement from the wheels and combine it with scan matching. SLAM will build a map and estimate the robot's position. Recognizing a previously scanned location should help correct accumulated drift. A saved map should later let the robot locate itself at the start of a new session.

The map will distinguish free space, obstacles and areas that have not been observed. A separate layer will track cleaning coverage: the LiDAR can see places the brushes have not cleaned. Route planning will account for the robot's dimensions, cleaning width and clearance from obstacles. Moving furniture, glass and objects outside the scan height need additional handling. Bumper and cliff sensors will remain part of the system.

## Hardware and power

Before choosing a sensor, check its interface, scan rate, range, field of view, voltage and current requirements, and software support we can run. The mount must give it a view around the robot without interfering with the bumper, movement or clearance under furniture.

The whole system is intended to run from the Roomba battery, without an external backup battery. The existing ESP 3.3V rail and OI supply cannot be assumed to have spare capacity for LiDAR. OI has a documented total continuous current limit of 200mA. Before adding hardware, calculate the load of all components, including startup current, and choose suitable power conversion and protection. Any different connection to the Roomba battery will need its own electrical design. This plan does not provide instructions for that connection.

## Work stages

1. Record original cleaning cycles and check that sensor readings and wheel counts arrive continuously.
2. Compare measured travel and turns with wheel-based estimates. Quantify slip and error, then calibrate the movement model.
3. Test the LiDAR separately. Check scan timing, range and disconnect handling, and save recordings that can be replayed.
4. Combine scans and wheel data while the robot runs its original cleaning program.
5. Check localization against a saved map at the start of another run, including after changes to the room.
6. Add route planning, room coverage and obstacle handling before connecting the planner to motion commands.

The current server records data; motion control is a future stage. Before enabling our own movement commands, we need a local stop mechanism for lost connections, speed limits and handling for safety sensors. A stop command sent over Wi-Fi alone is insufficient.

## Measuring progress

Repeat runs in the same room. Compare map dimensions with known measurements, return-to-start position error, cleaning time, covered area and repeated passes over cleaned areas. Test network outages, missing scans and moved furniture too. A convincing map image alone does not establish accurate localization or coverage.

Our first target is a combined recording we can replay and analyze. Sensor selection and mapping implementation can follow; the existing recorder can run before either is ready.
