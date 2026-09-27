#!/usr/bin/env bash
# Deploy ankibridge to a VPS under /opt/ankibridge.
#
#   server/study-bridge/deploy/atyaga/deploy.sh
#
# The target and the allowlist come from deploy.local.env beside this script
# (gitignored, like every *.local* file here) or from the environment:
#   ANKIBRIDGE_HOST=root@<server>
#   ANKIBRIDGE_ALLOWLIST=<AnkiWeb email>[,<another>]
#
# Runs the attack suite and the saved-words suite first, stages a build
# context the Dockerfile's COPY paths expect (the converter lives at the repo
# root, outside this directory), ships it, and rebuilds on the server. The
# server's .env (BRIDGE_FERNET_KEY, BRIDGE_ALLOWLIST) and data/ are never
# touched from here; a missing .env gets a fresh key and the allowlist below.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
[ -f "$HERE/deploy.local.env" ] && . "$HERE/deploy.local.env"
HOST="${ANKIBRIDGE_HOST:?set ANKIBRIDGE_HOST in deploy.local.env, e.g. root@203.0.113.7}"
ALLOWLIST="${ANKIBRIDGE_ALLOWLIST:?set ANKIBRIDGE_ALLOWLIST in deploy.local.env: the AnkiWeb account(s) it serves}"
DIR=/opt/ankibridge
SSH=(ssh -i "$HOME/.ssh/id_ed25519" -o BatchMode=yes "$HOST")

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
SRC="$REPO_ROOT/server/study-bridge"
PY="$SRC/.venv/bin/python"
[ -x "$PY" ] || { echo "no venv at $SRC/.venv; see server/study-bridge/README.md"; exit 1; }

echo "attack suite ..."
"$PY" "$SRC/tests/attack_test.py" | tail -1
echo "saved words ..."
"$PY" "$SRC/tests/test_words.py" | tail -1

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
rsync -a --exclude .venv --exclude data --exclude .env --exclude __pycache__ --exclude deploy --exclude tests \
  "$SRC/" "$STAGE/"
cp "$SRC/deploy/atyaga/compose.yaml" "$STAGE/compose.yaml"
mkdir -p "$STAGE/tools_local" "$STAGE/lib/EpdFont/scripts"
rsync -a --exclude __pycache__ "$REPO_ROOT/tools_local/study/" "$STAGE/tools_local/study/"
cp "$REPO_ROOT/lib/EpdFont/scripts/fontconvert_sdcard.py" "$REPO_ROOT/lib/EpdFont/scripts/cpfont_version.py" \
  "$STAGE/lib/EpdFont/scripts/"

"${SSH[@]}" "mkdir -p $DIR"
# --exclude .env and data/ are load-bearing with --delete: both exist only on
# the server, and this line would otherwise erase the secrets and the users.
rsync -a --delete --exclude .env --exclude data/ -e "ssh -i $HOME/.ssh/id_ed25519" "$STAGE/" "$HOST:$DIR/"

"${SSH[@]}" "cd $DIR && if [ ! -f .env ]; then
  printf 'BRIDGE_FERNET_KEY=%s\nBRIDGE_ALLOWLIST=%s\n' \"\$(docker run --rm python:3.13-slim python -c 'import base64,os;print(base64.urlsafe_b64encode(os.urandom(32)).decode())')\" '$ALLOWLIST' > .env
  chmod 600 .env; echo 'created .env'; fi"
# The bind mount must belong to the container's uid before its first write.
"${SSH[@]}" "mkdir -p $DIR/data && docker run --rm -v $DIR/data:/data alpine chown 10002:10002 /data"
"${SSH[@]}" "cd $DIR && docker compose up -d --build && docker builder prune -f >/dev/null && docker image prune -f >/dev/null"

echo "waiting for health ..."
for _ in $(seq 1 30); do
  if "${SSH[@]}" "cd $DIR && docker compose exec -T app python -c \"import urllib.request;print(urllib.request.urlopen('http://127.0.0.1:8080/healthz').read().decode())\"" 2>/dev/null; then
    "${SSH[@]}" "cd $DIR && docker compose ps"
    exit 0
  fi
  sleep 3
done
echo "not healthy after 90s:"
"${SSH[@]}" "cd $DIR && docker compose logs --tail 40"
exit 1
