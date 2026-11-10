#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/roocore-tests.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
"${CXX:-clang++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ilib/RoombaOI/src lib/RoombaOI/src/RoombaOI.cpp tests/roomba_oi_test.cpp \
  -o "$build_dir/roomba-oi-test"
"$build_dir/roomba-oi-test"

python3 tests/check_oi_map.py

"${CXX:-clang++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ilib/RooCoreLidarPower/src tests/lidar_power_test.cpp -o "$build_dir/lidar-power-test"
"$build_dir/lidar-power-test"

"${CXX:-clang++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ilib/RoombaOI/src -Ilib/RooNavigation/src lib/RoombaOI/src/RoombaOI.cpp \
  tests/power_shutdown_test.cpp -o "$build_dir/power-test"
"$build_dir/power-test"

"${CXX:-clang++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ilib/RoombaOI/src -Ilib/RooNavigation/src lib/RoombaOI/src/RoombaOI.cpp \
  tests/dirt_native_test.cpp -o "$build_dir/dirt-test"
"$build_dir/dirt-test"


"${CXX:-clang++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ilib/RooNavigation/src tests/carpet_native_test.cpp -o "$build_dir/carpet-test"
"$build_dir/carpet-test"

"${CXX:-clang++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ilib/RooNavigation/src tests/navigation_native_test.cpp \
  -o "$build_dir/navigation-test"
"$build_dir/navigation-test"

"${CXX:-clang++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ilib/RooNavigation/src tests/map_zones_native_test.cpp -o "$build_dir/map-zones-test"
"$build_dir/map-zones-test"
for case in dock_guard charge_resume; do
  "${CXX:-clang++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilib/RooNavigation/src "tests/${case}_test.cpp" -o "$build_dir/${case}-test"
  "$build_dir/${case}-test"
done
