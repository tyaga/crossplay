#!/bin/sh
# Builds and runs the tests for saving a looked-up word for Anki: the file
# naming, the header the importer parses, and the sentence the word was read
# in. WordCaptureCore is freestanding C++17, so no device and no PlatformIO.
#
#   host-tests/wordcapture/run.sh
set -e
cd "$(dirname "$0")"
# Keyed to this checkout: two worktrees sharing one build dir means one tree
# can run, and pass, a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-wordcapture-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  ../../src/util/WordCaptureCore.cpp test_word_capture.cpp -o "$BUILD_DIR/test_word_capture"
"$BUILD_DIR/test_word_capture"
