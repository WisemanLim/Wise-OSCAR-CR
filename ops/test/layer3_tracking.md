# Layer 3 — Multi-Object Tracking

## Scope
- `core/tracking/Tracker.{hpp,cpp}`
- Greedy IoU + ID lifecycle (spawn, update, age-out)
- `pipe.detections` → `pipe.tracks`

## Goal
Verify that the same physical object retains the same `id` across frames, and stale detections age out.

## Preconditions
- Layers 1 & 2 healthy
- Stub backend default produces a slowly-moving box → ideal for testing ID persistence

## Procedure

### 3.1 ID persistence
Sample `/api/live` twice with a 1-second gap and confirm at least one ID overlaps:
```bash
curl -fsS http://localhost:8080/api/live | jq -r '.[].id' | sort -u > /tmp/ids_a
sleep 1
curl -fsS http://localhost:8080/api/live | jq -r '.[].id' | sort -u > /tmp/ids_b
comm -12 /tmp/ids_a /tmp/ids_b
```
Expected: non-empty intersection (same ID seen across 1s).

### 3.2 Active track count
```bash
curl -fsS http://localhost:8080/api/stats | jq .tracker.active
```
Expected: ≥ 1 when stub/real detector is producing.

### 3.3 Trajectory shape
Each tracked object exposes `bbox`, `velocity_px`, plus a recent path through the localizer (`abs_pos` series).
```bash
curl -fsS http://localhost:8080/api/live | jq '.[0]'
```
Expected fields: `id`, `bbox`, `velocity_px`, `rel_pos`, `abs_pos`, `state`.

### 3.4 Age-out
Block frames (`docker pause oscar-core` or unplug camera) — after `track_buffer * frame_period` ms the active count should drop to 0.

## Pass criteria
| Check                 | Expected            |
|-----------------------|---------------------|
| Same ID across 1s     | yes                 |
| `tracker.active`      | > 0 when input live |
| Age-out               | active → 0 when frames stop |

## Failure modes
- IDs churn every frame → match threshold too strict; check `config.tracker.match_thresh`
- Tracks never age out → `track_buffer` too large
- Empty `/api/live` → detection step empty; re-run Layer 2

## Automation
```bash
ops/scripts/test_layer3_tracking.sh http://localhost:8080
```
