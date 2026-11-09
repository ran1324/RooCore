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
    -e 's/ROO_POWER_CUTOFF_VERIFIED false/ROO_POWER_CUTOFF_VERIFIED true/' \
    -e 's/ROO_POWER_CUTOFF_MV 0/ROO_POWER_CUTOFF_MV 13200/' \
    -e 's/ROO_POWER_RECOVER_MV 0/ROO_POWER_RECOVER_MV 13800/' \
    -e 's/ROO_POWER_RESTART_MIN_MV 0/ROO_POWER_RESTART_MIN_MV 14400/' \
    -e 's/ROO_POWER_REBOUND_MARGIN_MV 0/ROO_POWER_REBOUND_MARGIN_MV 400/' \
    -e 's/ROO_POWER_SAVE_MIN_MV 0/ROO_POWER_SAVE_MIN_MV 13000/' \
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
"$cli" compile --fqbn "$fqbn" --warnings all \
  --library "$PWD/lib/RoombaOI" --library "$PWD/lib/RooNavigation" --library "$PWD/lib/RooCoreLidarPower" \
  --build-path "$build_root/RooCoreXiao-commissioned" "$staged"
