#!/usr/bin/env bash
# Native run: build core + start oscar_core and vite dev server with one Ctrl+C
# to stop both. Used by `make up-local`.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$ROOT"

# ---------- deps ----------
need() { command -v "$1" >/dev/null 2>&1 || { echo "[up-local] missing: $1 — $2"; exit 1; }; }
need cmake "install via 'brew install cmake' (mac) or apt"
need npm   "install Node 20+ (https://nodejs.org)"

if [ "$(uname -s)" = "Darwin" ]; then
  if ! brew list opencv >/dev/null 2>&1; then
    echo "[up-local] OpenCV not found via Homebrew. Run: brew install opencv"
    exit 1
  fi
fi

# ---------- .env (optional) ----------
ENV_FILE="$ROOT/ops/.env"
if [ -f "$ENV_FILE" ]; then
  echo "[up-local] loading $ENV_FILE"
  # shellcheck disable=SC1090
  set -a; . "$ENV_FILE"; set +a
fi

# ---------- build ----------
echo "[up-local] building oscar_core (Release)…"
cmake -S "$ROOT/core" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$ROOT/build" -j

mkdir -p "$ROOT/data/videos"

# ---------- launch ----------
CORE_PID=""
WEB_PID=""
cleanup() {
  echo
  echo "[up-local] stopping…"
  [ -n "$CORE_PID" ] && kill "$CORE_PID" 2>/dev/null || true
  [ -n "$WEB_PID"  ] && kill "$WEB_PID"  2>/dev/null || true
  wait 2>/dev/null || true
}
trap cleanup INT TERM EXIT

echo "[up-local] starting oscar_core…"
"$ROOT/build/oscar_core" --config "$ROOT/config/oscar.json" &
CORE_PID=$!

echo "[up-local] starting vite dev (web)…"
( cd "$ROOT/web" && npm install --silent && npm run dev ) &
WEB_PID=$!

cat <<EOF

  oscar-core: http://localhost:${OSCAR_HTTP_PORT:-8080}
  oscar-web : http://localhost:5173  (HMR; proxies /api to core)

  Ctrl+C to stop both.

EOF

# Wait for whichever exits first (bash 3.2 compatible — no `wait -n`).
while kill -0 "$CORE_PID" 2>/dev/null && kill -0 "$WEB_PID" 2>/dev/null; do
  sleep 1
done
