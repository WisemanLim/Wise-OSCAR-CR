#!/usr/bin/env bash
source "$(dirname "$0")/_common.sh"

hdr "Layer 2: inference"
c1=$(req GET /api/stats | sed -n 's/.*"inference":{"count":\([0-9]*\).*/\1/p' | head -n1)
sleep 2
c2=$(req GET /api/stats | sed -n 's/.*"inference":{"count":\([0-9]*\).*/\1/p' | head -n1)
[ "${c2:-0}" -gt "${c1:-0}" ] || { echo "  FAIL: infer count stagnant $c1 -> $c2"; exit 1; }
ok "infer count advanced: $c1 -> $c2"

lat=$(req GET /api/stats | sed -n 's/.*"latency_ms":\([0-9.eE+-]*\).*/\1/p' | head -n1)
echo "  inference avg latency: ${lat:-?} ms"
ok "latency reported"

echo "[PASS] Layer 2"
