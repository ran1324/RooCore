#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cli=${ARDUINO_CLI:-arduino-cli}
# This custom endpoint uses the Matter APIs bundled with this exact SDK release.
"$cli" core list --format json | python3 -c '
import json, sys
installed = next((p.get("installed_version") for p in json.load(sys.stdin)["platforms"] if p["id"] == "esp32:esp32"), None)
if installed != "3.3.12":
    sys.exit("This simulator requires esp32:esp32@3.3.12; no SDK was changed automatically.")
'
build_dir=${ROOCORE_MATTER_BUILD_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/roocore-matter.XXXXXX")}
printf 'Simulator build only; no upload or serial connection. Output: %s\n' "$build_dir"
"$cli" compile --fqbn 'esp32:esp32:XIAO_ESP32S3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=default' \
  --warnings all --build-path "$build_dir" firmware/xiao/RooCoreMatterSim
