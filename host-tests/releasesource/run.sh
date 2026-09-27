#!/bin/sh
# A build for a fork of this fork asks its own repository for updates, and no
# other: a reader offered this fork's release would install a build without
# what the fork added. The default sources are host-tests/devreport's.
#
#   host-tests/releasesource/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-releasesource-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
cat > "$BUILD_DIR/test.cpp" <<'CPP'
#include <cstdio>
#include <cstring>

#include "network/ReleaseSources.h"

int main() {
  int failures = 0;
  if (release_sources::kCount != 1) {
    std::printf("  FAIL a fork's build asks one source, got %d\n", release_sources::kCount);
    failures++;
  }
  const char* want = "https://api.github.com/repos/someone/crossplay/releases/latest";
  if (std::strcmp(release_sources::kUrls[0], want) != 0) {
    std::printf("  FAIL the source is the fork's own releases: %s\n", release_sources::kUrls[0]);
    failures++;
  }
  std::printf("%s 2 checks, %d failed\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
CPP
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I../../src '-DCROSSPLAY_RELEASE_REPO="someone/crossplay"' \
  "$BUILD_DIR/test.cpp" -o "$BUILD_DIR/test"
"$BUILD_DIR/test"
