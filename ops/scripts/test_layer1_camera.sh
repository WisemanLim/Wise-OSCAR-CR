#!/usr/bin/env bash
source "$(dirname "$0")/_common.sh"

hdr "Layer 1: camera capture"
out="$TMP/stats.json"
req GET /api/stats > "$out"
ok "stats endpoint reachable"
grep -q '"camera"' "$out"
ok "stats contains 'camera' block"

seq1=$(req GET /api/stats | sed -n 's/.*"seq":\([0-9]*\).*/\1/p' | head -n1)
sleep 2
seq2=$(req GET /api/stats | sed -n 's/.*"seq":\([0-9]*\).*/\1/p' | head -n1)
[ "${seq2:-0}" -gt "${seq1:-0}" ] || { echo "  FAIL: seq did not advance ($seq1 -> $seq2)"; exit 1; }
ok "frame seq advanced: $seq1 -> $seq2"

req GET /api/frame.jpg --output "$TMP/f.jpg"
[ -s "$TMP/f.jpg" ] || { echo "  FAIL: empty frame"; exit 1; }
ok "frame.jpg downloaded ($(wc -c <"$TMP/f.jpg") bytes)"

echo "[PASS] Layer 1"
