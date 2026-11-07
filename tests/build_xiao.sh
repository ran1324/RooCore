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
  --library "$PWD/lib/RoombaOI" --library "$PWD/lib/RooNavigation" \
  --build-path "$build_root/RooCoreXiao" firmware/xiao/RooCoreXiao

# Compile the real actuator path too; this temporary config is never flashed.
staged="$build_root/commissioned/RooCoreXiao"
mkdir -p "$staged"
cp firmware/xiao/RooCoreXiao/RooCoreXiao.ino "$staged/"
cp firmware/xiao/RooCoreXiao/config.example.h "$staged/config.example.h"
sed -e 's/ROO_COMMISSIONED false/ROO_COMMISSIONED true/' \
    -e 's/ROO_ENCODERS_VERIFIED false/ROO_ENCODERS_VERIFIED true/' \
    -e 's/ROO_DOCK_EXIT_VERIFIED false/ROO_DOCK_EXIT_VERIFIED true/' \
    firmware/xiao/RooCoreXiao/config.example.h > "$staged/config.local.h"
"$cli" compile --fqbn "$fqbn" --warnings all \
  --library "$PWD/lib/RoombaOI" --library "$PWD/lib/RooNavigation" \
  --build-path "$build_root/RooCoreXiao-commissioned" "$staged"
