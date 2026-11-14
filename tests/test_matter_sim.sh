#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
# A regression guard on this standalone application, not proof about SDK/ROM boot output.
python3 - <<'PY'
from pathlib import Path
import re
for path in Path('firmware/xiao/RooCoreMatterSim').iterdir():
    if path.name.startswith('._') or path.suffix not in ('.ino', '.h', '.cpp'):
        continue
    source = path.read_text()
    assert not re.search(r'^\s*#include\s*[<"](?:RoombaOI|RooNavigation|RooCoreLidarPower)', source, re.M), path
    assert not re.search(r'\b(?:pinMode|digitalWrite|analogWrite|HardwareSerial|Serial1|Serial2)\b', source), path
print('PASS: application isolation guard (SDK/ROM UART boot output is outside this check)')
PY
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/roocore-matter-test.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer tests/matter_sim_test.cpp -o "$test_dir/matter-sim-test"
"$test_dir/matter-sim-test"
