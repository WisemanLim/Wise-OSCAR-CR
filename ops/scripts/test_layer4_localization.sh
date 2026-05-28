#!/usr/bin/env bash
source "$(dirname "$0")/_common.sh"

hdr "Layer 4: localization"
# Push pose
req POST /api/pose \
    -H "Content-Type: application/json" \
    -d '{"lat":37.5665,"lon":126.9780,"alt":50,"yaw":0,"pitch":0,"roll":0}'
ok "pose POST accepted"

p="$TMP/pose.json"
req GET /api/pose > "$p"
grep -q '"lat":37.566' "$p"
ok "pose persisted"

sleep 2
live="$TMP/live.json"
req GET /api/live > "$live"
grep -q '"abs_pos"' "$live"
ok "live tracks include abs_pos"
grep -q '"state"' "$live"
ok "objects have Moving/Stationary state field"

echo "[PASS] Layer 4"
