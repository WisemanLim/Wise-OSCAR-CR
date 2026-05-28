# Layer 4 — Dynamic Camera Localization

## Scope
- `core/localization/Localizer.{hpp,cpp}`
- `core/localization/PoseStore.hpp` (thread-safe latest camera pose)
- Ingests GNSS/IMU pose through `POST /api/pose`
- Computes per-object **relative** `(x_rel, y_rel, z_rel)` and **absolute** `(x_abs, y_abs, z_abs)` from pixel bbox + camera pose + intrinsics
- Velocity → `state: Moving | Stationary`

## Goal
1. Pose round-trips through `/api/pose`
2. Coordinate-transform output is populated and reasonable
3. State flag flips between `Moving`/`Stationary` based on world-velocity magnitude

## Preconditions
- Layers 1–3 healthy
- Camera intrinsics in `config.localization.{fx,fy,cx,cy}` roughly match the source

## Procedure

### 4.1 Push pose
```bash
curl -fsS -X POST http://localhost:8080/api/pose \
  -H 'Content-Type: application/json' \
  -d '{"lat":37.5665,"lon":126.9780,"alt":50,"yaw":0,"pitch":-10,"roll":0}'
```
Expected: `{"ok":true}`

### 4.2 Read pose
```bash
curl -fsS http://localhost:8080/api/pose | jq
```
Expected: matches body sent above.

### 4.3 Relative & absolute coords populated
```bash
curl -fsS http://localhost:8080/api/live | jq '.[0] | {id, bbox, rel_pos, abs_pos, state}'
```
Expected:
```json
{
  "id": 7,
  "bbox": [612, 330, 60, 60],
  "rel_pos": [0.12, 1.5, 6.4],
  "abs_pos": [0.12, 6.4, 50.0],
  "state": "Moving"
}
```
- `rel_pos.y` ≈ `camera_height_m - ground_height_m`
- `abs_pos` magnitude grows as relative depth grows
- `state` either `Moving` or `Stationary`

### 4.4 State transition
Stop the synthetic motion or hold a real object still for ~1 s:
```bash
watch -n 0.5 'curl -fsS http://localhost:8080/api/live | jq ".[].state"'
```
Expected: at least one object switches to `Stationary` when world-velocity drops below `static_velocity_thr`.

### 4.5 Yaw-only sanity check
```bash
curl -X POST .../api/pose -d '{"lat":37.5,"lon":127.0,"yaw":90,"pitch":0,"roll":0}'
```
Observe that `abs_pos` rotates accordingly (object that was "in front" now lies along the east axis).

## Pass criteria
| Check                                | Expected         |
|--------------------------------------|------------------|
| `POST /api/pose` 200                 | yes              |
| `rel_pos` and `abs_pos` non-zero     | yes              |
| `state ∈ {Moving, Stationary}`       | yes              |
| Pose update affects abs_pos          | yes              |

## Failure modes
- All abs_pos zero → pose never sent; verify `pose_known: true` in `/api/stats`
- `state` stuck on `Moving` → `static_velocity_thr` too low for sensor noise
- Wildly large rel_pos → intrinsics `(fx, fy)` mis-configured

## Automation
```bash
ops/scripts/test_layer4_localization.sh http://localhost:8080
```
