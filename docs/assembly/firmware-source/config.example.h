#pragma once
// Copy to config.local.h (gitignored). Initial builds are telemetry-only.
#define ROO_WIFI_SSID ""
#define ROO_WIFI_PASSWORD ""
// Unique 12-63 printable characters, provision privately; empty disables the setup AP.
#define ROO_SETUP_AP_PASSWORD ""
#define ROO_API_TOKEN "" // at least 24 random characters; trusted LAN only
// Outbound HTTPS to roocore.saymour.co.il. Separate per-device key; never use an admin password.
#define ROO_CLOUD_ENABLED false
#define ROO_CLOUD_DEVICE_TOKEN "" // provision privately; minimum 32 random characters
#define ROO_CLOUD_INTERVAL_MS 1000 // latest sensors/scan, software transport budget; not sensor acquisition rate
#define ROO_CLOUD_MAP_INTERVAL_MS 10000 // full map/coverage every 10 snapshots; no offline backlog
#define ROO_CLOUD_REQUIRE_POLICY true // hold until exact map zones are durably stored locally
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

// Trial policy: 13.8V warning; 13.5V for 5s return; 13.0V urgent; 12.5V stop.
// No D10/NPN software cutoff: the external 11k EN divider owns hardware cutoff.
// All verification flags stay false. Old ROO_POWER_CUTOFF_VERIFIED configs do not enable this.
#define ROO_POWER_POLICY_VERIFIED false
#define ROO_POWER_PASSIVE_STOP_VERIFIED false // confirmed stationary Safe -> Passive on this 620
#define ROO_POWER_HW_OFF_MAX_MV 0 // measured highest hardware cutoff across qualified conditions
#define ROO_POWER_RECOVER_MV 0 // measured/qualified rearm voltage, strictly above 13.8V
#define ROO_POWER_SAVE_MIN_MV 0 // measured safe flash-write floor, above hardware cutoff and <=12.5V
#define ROO_NATIVE_FALLBACK_VERIFIED false // Seek Dock and powered-off UART qualified on this 620
#define ROO_FALLBACK_CLEARANCE_VERIFIED false // full add-on height/envelope in the anchored dock corridor
// There is no default/precompiled approval. Explicit boot+mission-scoped approval is required.

// Passive recorder: base packet group 6; group 100 is an explicit unverified probe.
// This flag permits requesting extensions; it does NOT mark them hardware verified.
#define ROO_RECORD_EXTENDED_ENABLED false
// OTA is firmware-only, authenticated and restricted to explicit stopped service hold.
// No guessed battery threshold: qualify voltage stability/flash writes before enabling.
#define ROO_OTA_ENABLED false
#define ROO_OTA_PASSWORD "" // separate random secret, >=24 characters; never reuse API token
#define ROO_OTA_POWER_VERIFIED false
#define ROO_OTA_MIN_MV 0 // measured safe flash-write floor, including hardware cutoff margin
#define ROO_FIRMWARE_VERSION "native-recorder-v1"

// Battery observations and learned software return reserve. No hardware threshold writes.
#define ROO_BATTERY_PACK_ID "" // stable label for THIS physical battery; empty = no durable/qualified model
#define ROO_BATTERY_CALIBRATION_ID "" // measurement record ID; changing it isolates old learning
#define ROO_BATTERY_CURRENT_VERIFIED false // includes all RooCore loads and both charge/discharge signs
#define ROO_BATTERY_GAUGE_VERIFIED false // SOC reference qualified on the installed replacement pack
#define ROO_BATTERY_FULL_REFERENCE_VERIFIED false // native maintenance/full indication physically qualified
#define ROO_BATTERY_CURRENT_ERROR_MA 0.0f // measured worst-case current error, not a guessed accuracy
#define ROO_BATTERY_GAUGE_ERROR_MAH 0.0f // measured/reference bound for reported charge changes
#define ROO_BATTERY_MIN_C 0 // qualified measurement/model temperature domain
#define ROO_BATTERY_MAX_C 0
#define ROO_BATTERY_RESERVE_MARGIN_MAH 0.0f // measured additional docking reserve; 0 blocks adaptive return
#define ROO_BATTERY_ADAPTIVE_RETURN_ENABLED false // qualified learning may ONLY advance return
#define ROO_BATTERY_STORAGE_VERIFIED false // bounded Flash writes while already stopped at safe voltage
