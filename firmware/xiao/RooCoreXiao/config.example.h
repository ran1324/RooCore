#pragma once
// Copy to config.local.h (gitignored). Initial builds are telemetry-only.
#define ROO_WIFI_SSID ""
#define ROO_WIFI_PASSWORD ""
#define ROO_API_TOKEN "" // at least 24 random characters; trusted LAN only
#define ROO_COMMISSIONED false // true ONLY after the checks in docs/XIAO_NAVIGATION.md
#define ROO_ENCODERS_VERIFIED false // packets 43/44 verified on this Roomba 620
// Permanent 3.3V UART: D4/GPIO5 -> LiDAR RX, D5/GPIO6 <- LiDAR TX; D3/GPIO4 -> S1 ON.
// Firmware integration is complete; only actual bench qualification may set these.
// Zero / older configs keep the rail OFF. No guessed 500 ms startup delay.
#define ROO_LIDAR_POWER_VERIFIED false
#define ROO_LIDAR_RISE_MS 0 // measured rail rise + margin, before UART connects (milliseconds)
#define ROO_LIDAR_YAW_DEGREES 0.0f // measure mounting orientation, CCW from robot forward
#define ROO_LIDAR_CLOCKWISE true
#define ROO_LIDAR_X_METERS 0.0f // measured offset from wheel axle midpoint
#define ROO_LIDAR_Y_METERS 0.0f
#define ROO_METERS_PER_TICK 0.000444565f // nominal; calibrate real wheels
#define ROO_WHEEL_SEPARATION 0.235f // nominal; calibrate turns

#define ROO_DOCK_EXIT_VERIFIED false // known clear, level floor corridor behind the dock
#define ROO_DOCK_EXIT_METERS 0.60f // measure approach/exit distance on the real base

// Native charger remains responsible for the battery. These qualify departure only.
#define ROO_CHARGE_DEPARTURE_VERIFIED false
#define ROO_CHARGE_MISSION_READY false // false = native completion; true = mission energy policy
#define ROO_CHARGE_RESUME_PERCENT 80.0f // mission policy, NOT a battery longevity ceiling
#define ROO_CHARGE_NATIVE_MIN_PERCENT 100.0f // qualify with native maintenance state on actual pack
#define ROO_CHARGE_MIN_C 0 // actual pack-temperature operating bounds require validation
#define ROO_CHARGE_MAX_C 0 // equal bounds deliberately prevent departure
#define ROO_PROGRESS_INTERVAL_MS 1000 // initial telemetry cadence, not a physical timing guarantee

// Measured Home Base/cable footprint and swept body clearance. Zero = motion held.
#define ROO_DOCK_GEOMETRY_VERIFIED false
#define ROO_DOCK_FRONT_X_M 0.0f
#define ROO_DOCK_FRONT_Y_M 0.0f
#define ROO_DOCK_OUTWARD_YAW_RAD 0.0f
#define ROO_DOCK_DEPTH_M 0.0f
#define ROO_DOCK_HALF_WIDTH_M 0.0f
#define ROO_DOCK_MARGIN_M 0.0f
#define ROO_DOCK_BODY_RADIUS_M 0.0f // bounds body and fixed additions
#define ROO_DOCK_CORRIDOR_MIN_M 0.0f
#define ROO_DOCK_CORRIDOR_MAX_M 0.0f
#define ROO_DOCK_CORRIDOR_HALF_WIDTH_M 0.0f
#define ROO_DOCK_HEADING_TOLERANCE_RAD 0.0f
#define ROO_DOCK_MAX_LINEAR_MPS 0.0f
#define ROO_DOCK_MAX_ANGULAR_RPS 0.0f

// Approximate user dimensions, 2026-11-12; not floor-contact calibration.
// Offset assumes axle aligned with the leading edge of the 160 x 70 mm rectangle.
// Side pivot remains provisional; see docs/assembly/ROBOT_GEOMETRY.md.
#define ROO_BRUSH_GEOMETRY_CALIBRATED false
#define ROO_BRUSH_WIDTH_METERS 0.16f
#define ROO_BRUSH_DEPTH_METERS 0.07f
#define ROO_BRUSH_OFFSET_METERS -0.035f
#define ROO_SIDE_BRUSH_X_METERS 0.10f
#define ROO_SIDE_BRUSH_Y_METERS -0.13f // right of centre, positive Y is left
#define ROO_SIDE_BRUSH_RADIUS_METERS 0.07f
#define ROO_PERIMETER_ENABLED true // bench-test wall clearance before commissioning

#define ROO_WALL_TOUCH_VERIFIED false // optional 1.5 cm/s contact; verify bumper and stop latency first
#define ROO_WALL_IR_TARGET 0 // raw packet 27 at verified right-wall gap; 0 = observe only
#define ROO_ASSISTANCE_ENABLED true // isolated local cleaning + authenticated server assistance

// Optional load-based carpet estimate. No physical calibration has been performed.
#define ROO_CARPET_CURRENT_VERIFIED false // packet 56 verified separately from encoders
#define ROO_CARPET_BOOST_ENABLED false
#define ROO_CARPET_MIN_MV 0 // measured voltage envelope for BOTH calibration profiles
#define ROO_CARPET_MAX_MV 0
// Minimum plausible current, floor-exit threshold, carpet-entry threshold, overload ceiling.
#define ROO_CARPET_85_MIN_MA 0
#define ROO_CARPET_85_EXIT_MA 0
#define ROO_CARPET_85_ENTER_MA 0
#define ROO_CARPET_85_MAX_MA 0
#define ROO_CARPET_127_MIN_MA 0
#define ROO_CARPET_127_EXIT_MA 0
#define ROO_CARPET_127_ENTER_MA 0
#define ROO_CARPET_127_MAX_MA 0

// DirtDetect: collection/analysis ONLY. There is deliberately no auto-cleaning switch.
#define ROO_DIRT_THRESHOLD 0 // 0 disables region detection; retain raw readings
#define ROO_DIRT_CONSECUTIVE 5
#define ROO_DIRT_MIN_HIGH_MS 300
#define ROO_DIRT_MAX_GAP_MS 200
#define ROO_DIRT_REGION_RADIUS_M 0.30f
#define ROO_DIRT_REVISIT_BUDGET_MS 20000 // advisory plan / passive comparison limit
#define ROO_STASIS_VERIFIED false // optional packet 58; verify on this 620 first

// Automatic D10 -> NPN -> D36 EN cutoff. Complete code; hardware calibration required.
// With missing/invalid calibration, telemetry works but commissioned motion is held.
// See docs/POWER_SHUTDOWN.md. Do NOT copy synthetic build-test values to a robot.
#define ROO_POWER_CUTOFF_VERIFIED false
#define ROO_POWER_CUTOFF_MV 0 // loaded voltage: stop and prepare after 2 s continuously low
#define ROO_POWER_RECOVER_MV 0 // stable voltage before allowing a new explicit motion request
#define ROO_POWER_RESTART_MIN_MV 0 // measured LOWEST EN turn-on voltage across conditions
#define ROO_POWER_REBOUND_MARGIN_MV 0 // measured post-cut rebound + voltage-measurement error
#define ROO_POWER_SAVE_MIN_MV 0 // measured safe flash-write voltage above hardware EN cutoff
