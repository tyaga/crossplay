#!/bin/bash
# Publish a release of this fork's reader build on tyaga/crossplay, where the
# x4pro_anki updater looks (CROSSPLAY_RELEASE_REPO in platformio.ini).
#
#   ./scripts_local/release-anki.sh            # build, check, publish v<version>
#   ./scripts_local/release-anki.sh --dry-run  # build and check, publish nothing
#
# The version is [anki] version in platformio.ini, committed before this runs:
# the image compiles it in, and a release whose image reports a different
# number is one the reader offers again after installing it. The asset is
# named firmware.bin because that is the name the X4 Pro's updater asks for
# (CROSSPOINT_RELEASE_ASSET).
set -euo pipefail
cd "$(dirname "$0")/.."
REPO=tyaga/crossplay
DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

VERSION="$(awk '/^\[anki\]/{f=1;next} /^\[/{f=0} f&&/^version *=/{print $3; exit}' platformio.ini)"
[ -n "$VERSION" ] || { echo "no [anki] version in platformio.ini"; exit 1; }
TAG="v$VERSION"

if [ "$DRY" = 0 ]; then
  [ -z "$(git status --porcelain)" ] || { echo "uncommitted changes: commit the release first"; exit 1; }
  if gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1; then
    echo "$TAG is already released on $REPO: bump [anki] version first"
    exit 1
  fi
fi

export PATH="$HOME/.local/bin:$PATH"
pio run -e x4pro_anki
IMAGE=.pio/build/x4pro_anki/firmware.bin

python3 - "$IMAGE" "$VERSION" <<'PY'
import sys
image, version = open(sys.argv[1], "rb").read(), sys.argv[2]
problems = []
if image[0] != 0xE9:
    problems.append("not an ESP32 image")
if image[12] != 9:
    problems.append(f"chip id {image[12]}, not 9 (ESP32-S3): flashing it would brick an X4 Pro")
if len(image) > 0x7F0000:
    problems.append("larger than the OTA slot")
if f"{version}-anki".encode() not in image:
    problems.append(f"does not report {version}-anki")
if b"repos/tyaga/crossplay/releases/latest" not in image:
    problems.append("does not update from tyaga/crossplay")
if problems:
    sys.exit("refusing to publish: " + "; ".join(problems))
print(f"image ok: {len(image)} bytes, ESP32-S3, {version}-anki")
PY

if [ "$DRY" = 1 ]; then
  echo "dry run: $TAG not published"
  exit 0
fi

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp "$IMAGE" "$STAGE/firmware.bin"
gh release create "$TAG" "$STAGE/firmware.bin" --repo "$REPO" --target "$(git rev-parse HEAD)" \
  --title "$TAG" --notes "Reader build $VERSION-anki from $(git rev-parse --short HEAD)."
echo "published $TAG on $REPO"
