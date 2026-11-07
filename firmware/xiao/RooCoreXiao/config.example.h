#pragma once
// Copy to config.local.h (gitignored). Initial builds are telemetry-only.
#define ROO_WIFI_SSID ""
#define ROO_WIFI_PASSWORD ""
#define ROO_API_TOKEN "" // at least 24 random characters; trusted LAN only
#define ROO_COMMISSIONED false // true ONLY after the checks in docs/XIAO_NAVIGATION.md
#define ROO_ENCODERS_VERIFIED false // packets 43/44 verified on this Roomba 620
#define ROO_LIDAR_YAW_DEGREES 0.0f // measure mounting orientation, CCW from robot forward
#define ROO_LIDAR_CLOCKWISE true
#define ROO_LIDAR_X_METERS 0.0f // measured offset from wheel axle midpoint
#define ROO_LIDAR_Y_METERS 0.0f
#define ROO_METERS_PER_TICK 0.000444565f // nominal; calibrate real wheels
#define ROO_WHEEL_SEPARATION 0.235f // nominal; calibrate turns

#define ROO_DOCK_EXIT_VERIFIED false // known clear, level floor corridor behind the dock
#define ROO_DOCK_EXIT_METERS 0.60f // measure approach/exit distance on the real base

// Provisional geometry accepted for development; replace after measuring the robot.
#define ROO_BRUSH_GEOMETRY_CALIBRATED false
#define ROO_BRUSH_WIDTH_METERS 0.16f
#define ROO_BRUSH_DEPTH_METERS 0.04f
#define ROO_BRUSH_OFFSET_METERS 0.0f
#define ROO_SIDE_BRUSH_X_METERS 0.10f
#define ROO_SIDE_BRUSH_Y_METERS -0.13f // right of centre, positive Y is left
#define ROO_SIDE_BRUSH_RADIUS_METERS 0.10f
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
