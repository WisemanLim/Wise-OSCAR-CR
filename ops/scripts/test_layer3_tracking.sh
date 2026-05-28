#!/usr/bin/env bash
source "$(dirname "$0")/_common.sh"

hdr "Layer 3: tracking"
sleep 2
out="$TMP/live.json"
req GET /api/live > "$out"
ok "live endpoint reachable"

# Expect at least one tracked object with id
ids=$(grep -o '"id":[0-9]*' "$out" | head -n5)
[ -n "$ids" ] || { echo "  FAIL: no track ids in $(<"$out")"; exit 1; }
echo "  ids: $ids"
ok "tracker emitted at least one object id"

echo "[PASS] Layer 3"
