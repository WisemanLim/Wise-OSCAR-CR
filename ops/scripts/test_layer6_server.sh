#!/usr/bin/env bash
source "$(dirname "$0")/_common.sh"

hdr "Layer 6: server"
req GET /api/health > "$TMP/h.json"
grep -q '"ok":true' "$TMP/h.json"
ok "/api/health -> ok"

# SSE: capture a few seconds of stream and confirm 'data:' lines
( curl -fsS --max-time 4 "$BASE/api/events" > "$TMP/sse.txt" || true )
grep -q '^data: ' "$TMP/sse.txt" || grep -q ': ping' "$TMP/sse.txt"
ok "SSE /api/events produced data or heartbeat"

# CORS check
req GET /api/health -D "$TMP/headers.txt" -o /dev/null
grep -qi 'Access-Control-Allow-Origin: \*' "$TMP/headers.txt"
ok "CORS headers present"

echo "[PASS] Layer 6"
