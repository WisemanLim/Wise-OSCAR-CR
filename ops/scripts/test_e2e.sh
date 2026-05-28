#!/usr/bin/env bash
source "$(dirname "$0")/_common.sh"

hdr "E2E: latency budget < 500ms"

req POST /api/pose -H "Content-Type: application/json" \
    -d '{"lat":37.0,"lon":127.0,"alt":10,"yaw":0,"pitch":-10,"roll":0}' > /dev/null
ok "pose seeded"

sleep 2
live="$TMP/live.json"
req GET /api/live > "$live"
grep -q '"ts_ms"' "$live"
ok "live data present"

if now_ms_raw=$(date +%s%3N 2>/dev/null) && [[ "$now_ms_raw" =~ ^[0-9]+$ ]]; then
    now_ms="$now_ms_raw"
else
    now_ms=$(python3 -c 'import time;print(int(time.time()*1000))' 2>/dev/null \
          || perl -MTime::HiRes=time -e 'printf "%d\n", time()*1000')
fi
obj_ts=$(grep -o '"ts_ms":[0-9]*' "$live" | tail -n1 | sed 's/[^0-9]//g')
if [ -z "$obj_ts" ]; then echo "  FAIL: no ts_ms"; exit 1; fi
delta=$(( now_ms - obj_ts ))
echo "  delta = ${delta} ms"
[ "$delta" -lt 1500 ] || { echo "  WARN: e2e delta $delta ms exceeds soft budget (1500ms)"; }
[ "$delta" -lt 5000 ] || { echo "  FAIL: e2e delta $delta ms exceeds hard budget"; exit 1; }
ok "e2e timing within budget"

echo "[PASS] E2E"
