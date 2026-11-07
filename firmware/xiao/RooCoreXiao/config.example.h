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
