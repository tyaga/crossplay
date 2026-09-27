#!/bin/sh
# Builds and runs the reveal sweep. Same freestanding build as host-tests/ui:
# no device, no PlatformIO, no src/ or lib/ on the include path.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-revealsweep-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
SDK=../../freeink-sdk/libs/ui/FreeInkUI
ICONS=../../freeink-sdk/libs/assets/Icons
mkdir -p "$BUILD_DIR"
# -Wno-format-truncation is deliberately NOT here: it hid seven of the
# buffers card 256 fixed. The screens below are compiled with -Werror in
# host-tests/ui, which is where this class actually stops a build.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wno-comment \
  -I"$SDK/include" -I"$ICONS/include" \
  "$SDK/src/FreeInkUI.cpp" \
  ../../src/apps_local/battleship/BattleshipScreens.cpp \
  ../../src/apps_local/link/LinkScreens.cpp \
  ../../src/apps_local/study/StudyScreens.cpp \
  ../../src/apps_local/player/PlayerAvatar.cpp \
  ../../src/apps_local/player/PlayerName.cpp \
  sweep.cpp -o "$BUILD_DIR/sweep"
"$BUILD_DIR/sweep"
