#!/usr/bin/env bash
source "$(dirname "$0")/_common.sh"

hdr "Layer 5: storage"
r1=$(req GET /api/stats | sed -n 's/.*"storage":{"rows":\([0-9]*\).*/\1/p' | head -n1)
sleep 3
r2=$(req GET /api/stats | sed -n 's/.*"storage":{"rows":\([0-9]*\).*/\1/p' | head -n1)
[ "${r2:-0}" -gt "${r1:-0}" ] || { echo "  FAIL: row count stagnant ($r1 -> $r2)"; exit 1; }
ok "DB rows growing: $r1 -> $r2"

req GET '/api/tracks?limit=5' > "$TMP/tr.json"
grep -q '"object_id"' "$TMP/tr.json"
ok "/api/tracks returns persisted rows"

# Pick an object id and fetch its history
oid=$(grep -o '"object_id":[0-9]*' "$TMP/tr.json" | head -n1 | sed 's/[^0-9]//g')
if [ -n "$oid" ]; then
    req GET "/api/tracks/$oid" > "$TMP/byid.json"
    grep -q "\"object_id\":$oid" "$TMP/byid.json"
    ok "history for object_id=$oid retrievable"
fi

echo "[PASS] Layer 5"
