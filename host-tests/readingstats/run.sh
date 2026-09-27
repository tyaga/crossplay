#!/bin/sh
# Builds and runs the reading-stats tests. StatsCore is freestanding C++17, so
# no device and no PlatformIO are needed.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-readingstats-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/readingstats

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$SRC/StatsCore.cpp" test_core.cpp -o "$BUILD_DIR/test_core"

"$BUILD_DIR/test_core"
