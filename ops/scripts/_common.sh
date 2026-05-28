#!/usr/bin/env bash
set -euo pipefail
BASE="${1:-http://localhost:8080}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

req() {
    local method="$1"; shift
    local path="$1"; shift
    if [[ "$method" == "GET" ]]; then
        curl -fsS "$BASE$path" "$@"
    else
        curl -fsS -X "$method" "$BASE$path" "$@"
    fi
}

assert_contains() {
    if ! grep -q -- "$1" "$2"; then
        echo "  ASSERT FAIL: '$1' not found in $2"
        cat "$2"
        return 1
    fi
}

ok()  { echo "  ✓ $*"; }
hdr() { echo "::: $* :::"; }
