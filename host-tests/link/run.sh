#!/bin/sh
# Builds and runs the local-multiplayer tests. No device and no PlatformIO:
# the protocol and the session are freestanding C++17, and they were written
# that way so a match can be played on a laptop against a link that drops,
# delays, duplicates and reorders on purpose.
#
#   host-tests/link/run.sh
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name. Two worktrees sharing
# one build dir means one tree can run -- and pass -- a binary the other
# built, which is a green suite whose source is not even present.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-link-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/link
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/LinkProtocol.cpp $SRC/LinkSession.cpp \
  test_link.cpp -o "$BUILD_DIR/test_link"
"$BUILD_DIR/test_link"

# The transport, over real sockets. Two Radio instances bind their own UDP ports
# on localhost and exchange real datagrams, which is the same path two simulator
# windows take. The device (ESP-NOW) branch is compiled out and stays
# hardware-gated.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/LinkProtocol.cpp $SRC/LinkSession.cpp \
  $SRC/LinkRadio.cpp test_radio.cpp -o "$BUILD_DIR/test_radio"
"$BUILD_DIR/test_radio"

# The flow layer: what a game actually uses. Held to the same hostile link, and
# deliberately driven through the loop ordering a game author would get wrong.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/LinkProtocol.cpp $SRC/LinkSession.cpp \
  $SRC/LinkRadio.cpp $SRC/LinkPlay.cpp test_play.cpp -o "$BUILD_DIR/test_play"
"$BUILD_DIR/test_play"

# A real game of Battleship between two devices: the actual rules over the same
# hostile link. It stresses a secret each side keeps inside a shared state, and
# a setup phase the box plays simultaneously that the wire can only alternate.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/LinkProtocol.cpp $SRC/LinkSession.cpp \
  $SRC/LinkRadio.cpp $SRC/LinkPlay.cpp ../../src/apps_local/battleship/BattleshipCore.cpp \
  test_battleshiplink.cpp -o "$BUILD_DIR/test_battleshiplink"
"$BUILD_DIR/test_battleshiplink"
