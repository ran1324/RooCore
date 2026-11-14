#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cli=${ARDUINO_CLI:-arduino-cli}
fqbn='esp32:esp32:XIAO_ESP32S3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB'
build_root=${ROOCORE_BUILD_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/roocore-xiao.XXXXXX")}
printf 'Build directory: %s\n' "$build_root"
"$cli" version
"$cli" core list
for example in BatteryMonitor LinkSetup; do
  "$cli" compile --fqbn "$fqbn" --warnings all \
    --library "$PWD/lib/RoombaOI" \
    --build-path "$build_root/$example" \
    "lib/RoombaOI/examples/$example"
done

"$cli" compile --fqbn "$fqbn" --warnings all \
  --library "$PWD/lib/RoombaOI" --library "$PWD/lib/RooNavigation" --library "$PWD/lib/RooCoreLidarPower" \
  --build-path "$build_root/RooCoreXiao" firmware/xiao/RooCoreXiao

# Compile the real actuator and power-policy paths too. Synthetic power/dirt/carpet thresholds
# below are test fixtures, NOT calibration. This temporary config is never flashed.
staged="$build_root/commissioned/RooCoreXiao"
mkdir -p "$staged"
cp firmware/xiao/RooCoreXiao/RooCoreXiao.ino "$staged/"
cp firmware/xiao/RooCoreXiao/config.example.h "$staged/config.example.h"
sed -e 's/ROO_COMMISSIONED false/ROO_COMMISSIONED true/' \
    -e 's/ROO_ENCODERS_VERIFIED false/ROO_ENCODERS_VERIFIED true/' \
    -e 's/ROO_LIDAR_POWER_VERIFIED false/ROO_LIDAR_POWER_VERIFIED true/' \
    -e 's/ROO_LIDAR_RISE_MS 0/ROO_LIDAR_RISE_MS 25/' \
    -e 's/ROO_POWER_POLICY_VERIFIED false/ROO_POWER_POLICY_VERIFIED true/' \
    -e 's/ROO_POWER_PASSIVE_STOP_VERIFIED false/ROO_POWER_PASSIVE_STOP_VERIFIED true/' \
    -e 's/ROO_NATIVE_FALLBACK_VERIFIED false/ROO_NATIVE_FALLBACK_VERIFIED true/' \
    -e 's/ROO_FALLBACK_CLEARANCE_VERIFIED false/ROO_FALLBACK_CLEARANCE_VERIFIED true/' \
    -e 's/ROO_POWER_HW_OFF_MAX_MV 0/ROO_POWER_HW_OFF_MAX_MV 12200/' \
    -e 's/ROO_POWER_RECOVER_MV 0/ROO_POWER_RECOVER_MV 14000/' \
    -e 's/ROO_POWER_SAVE_MIN_MV 0/ROO_POWER_SAVE_MIN_MV 12400/' \
    -e 's/ROO_STASIS_VERIFIED false/ROO_STASIS_VERIFIED true/' \
    -e 's/ROO_DIRT_THRESHOLD 0/ROO_DIRT_THRESHOLD 100/' \
    -e 's/ROO_DOCK_EXIT_VERIFIED false/ROO_DOCK_EXIT_VERIFIED true/' \
    -e 's/ROO_CARPET_CURRENT_VERIFIED false/ROO_CARPET_CURRENT_VERIFIED true/' \
    -e 's/ROO_CARPET_BOOST_ENABLED false/ROO_CARPET_BOOST_ENABLED true/' \
    -e 's/ROO_CARPET_MIN_MV 0/ROO_CARPET_MIN_MV 14000/' \
    -e 's/ROO_CARPET_MAX_MV 0/ROO_CARPET_MAX_MV 16000/' \
    -e 's/ROO_CARPET_85_MIN_MA 0/ROO_CARPET_85_MIN_MA 50/' \
    -e 's/ROO_CARPET_85_EXIT_MA 0/ROO_CARPET_85_EXIT_MA 200/' \
    -e 's/ROO_CARPET_85_ENTER_MA 0/ROO_CARPET_85_ENTER_MA 300/' \
    -e 's/ROO_CARPET_85_MAX_MA 0/ROO_CARPET_85_MAX_MA 600/' \
    -e 's/ROO_CARPET_127_MIN_MA 0/ROO_CARPET_127_MIN_MA 80/' \
    -e 's/ROO_CARPET_127_EXIT_MA 0/ROO_CARPET_127_EXIT_MA 350/' \
    -e 's/ROO_CARPET_127_ENTER_MA 0/ROO_CARPET_127_ENTER_MA 500/' \
    -e 's/ROO_CARPET_127_MAX_MA 0/ROO_CARPET_127_MAX_MA 900/' \
    firmware/xiao/RooCoreXiao/config.example.h > "$staged/config.local.h"

# Exercise the new charger and dock-guard branches too. These dimensions and
# temperatures are synthetic compiler fixtures, not measurements or pack limits.
python3 - "$staged/config.local.h" <<'PY'
from pathlib import Path
import re
import sys
p = Path(sys.argv[1])
text = p.read_text()
values = {
    'ROO_BATTERY_PACK_ID': '"synthetic-pack"',
    'ROO_BATTERY_CALIBRATION_ID': '"synthetic-calibration"',
    'ROO_BATTERY_CURRENT_VERIFIED': 'true', 'ROO_BATTERY_GAUGE_VERIFIED': 'true',
    'ROO_BATTERY_FULL_REFERENCE_VERIFIED': 'true',
    'ROO_BATTERY_CURRENT_ERROR_MA': '20.0f', 'ROO_BATTERY_GAUGE_ERROR_MAH': '5.0f',
    'ROO_BATTERY_MIN_C': '10', 'ROO_BATTERY_MAX_C': '40',
    'ROO_BATTERY_RESERVE_MARGIN_MAH': '50.0f',
    'ROO_BATTERY_ADAPTIVE_RETURN_ENABLED': 'true', 'ROO_BATTERY_STORAGE_VERIFIED': 'true',

    'ROO_SETUP_AP_PASSWORD': '"synthetic-setup-only-12345"',
    'ROO_CLOUD_ENABLED': 'true',
    'ROO_CLOUD_DEVICE_TOKEN': '"synthetic-compile-only-device-key-not-a-credential"',
    'ROO_RECORD_EXTENDED_ENABLED': 'true',
    'ROO_OTA_ENABLED': 'true',
    'ROO_OTA_PASSWORD': '"synthetic-build-only-password-123"',
    'ROO_OTA_POWER_VERIFIED': 'true',
    'ROO_OTA_MIN_MV': '14500',
    'ROO_CHARGE_DEPARTURE_VERIFIED': 'true',
    'ROO_CHARGE_MIN_C': '10', 'ROO_CHARGE_MAX_C': '40',
    'ROO_BRUSH_GEOMETRY_CALIBRATED': 'true',
    'ROO_DOCK_GEOMETRY_VERIFIED': 'true',
    'ROO_DOCK_FRONT_X_M': '.18f', 'ROO_DOCK_OUTWARD_YAW_RAD': '3.14159265f',
    'ROO_DOCK_DEPTH_M': '.20f', 'ROO_DOCK_HALF_WIDTH_M': '.16f',
    'ROO_DOCK_MARGIN_M': '.03f', 'ROO_DOCK_BODY_RADIUS_M': '.17f',
    'ROO_DOCK_CORRIDOR_MIN_M': '.10f', 'ROO_DOCK_CORRIDOR_MAX_M': '1.20f',
    'ROO_DOCK_CORRIDOR_HALF_WIDTH_M': '.12f', 'ROO_DOCK_HEADING_TOLERANCE_RAD': '.12f',
    'ROO_DOCK_MAX_LINEAR_MPS': '.30f', 'ROO_DOCK_MAX_ANGULAR_RPS': '1.5f',
}
for key, value in values.items():
    text, count = re.subn(r'(?m)^(#define ' + key + r')\s+\S+', r'\g<1> ' + value, text)
    assert count == 1, key
p.write_text(text)
PY
"$cli" compile --fqbn "$fqbn" --warnings all \
  --library "$PWD/lib/RoombaOI" --library "$PWD/lib/RooNavigation" --library "$PWD/lib/RooCoreLidarPower" \
  --build-path "$build_root/RooCoreXiao-commissioned" "$staged"
