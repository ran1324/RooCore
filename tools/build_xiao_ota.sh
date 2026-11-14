#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=${ROOCORE_OTA_BUILD_DIR:-"$PWD/output/xiao-ota"}
mkdir -p "$build_dir"
"${ARDUINO_CLI:-arduino-cli}" compile \
  --fqbn 'esp32:esp32:XIAO_ESP32S3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB' \
  --warnings all --library "$PWD/lib/RoombaOI" --library "$PWD/lib/RooNavigation" \
  --library "$PWD/lib/RooCoreLidarPower" --build-path "$build_dir" firmware/xiao/RooCoreXiao
python3 tools/xiao_capture.py manifest "$build_dir/RooCoreXiao.ino.bin" > "$build_dir/manifest.json"
printf 'Application manifest: %s/manifest.json\n' "$build_dir"
