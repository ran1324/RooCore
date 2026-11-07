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
