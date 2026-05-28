# Layer 1 — Camera Capture

## Scope
- `core/camera/CameraSource.{hpp,cpp}`
- OpenCV `VideoCapture` (USB/RTSP/index) → `pipe.frames`, `pipe.frames_rec`, `pipe.frames_ws`
- Fallback synthetic frame generator when OpenCV unavailable

## Goal
Verify that the capture thread produces frames at the target rate and surfaces them via the live snapshot endpoint.

## Preconditions
- `oscar-core` reachable on `TEST_BASE` (default `http://localhost:8080`)
- `config/oscar.json` → `camera.source` set (`"0"` for first webcam, `"/dev/video0"`, or `rtsp://...`)

## Procedure

### 1.1 Stats endpoint
```bash
curl -fsS http://localhost:8080/api/stats | jq .camera
```
Expected:
```json
{ "connected": true, "fps": 25.4, "seq": 142 }
```

### 1.2 Sequence number advances
```bash
A=$(curl -fsS http://localhost:8080/api/stats | jq -r .camera.seq)
sleep 2
B=$(curl -fsS http://localhost:8080/api/stats | jq -r .camera.seq)
echo "seq: $A -> $B"
```
Expected: `B > A`, ideally `(B - A) ≈ target_fps * 2`.

### 1.3 Live frame snapshot
```bash
curl -fsS http://localhost:8080/api/frame.jpg -o /tmp/frame.jpg
file /tmp/frame.jpg
```
Expected: non-empty file, `JPEG image data`.

### 1.4 Frame dimensions
Cross-check `frame.jpg` dimensions match `config.camera.{width,height}`:
```bash
identify /tmp/frame.jpg     # ImageMagick — optional
```

## Pass criteria
| Check                       | Expected                |
|-----------------------------|-------------------------|
| `/api/stats.camera.connected` | `true`                |
| `seq` monotonic in 2s window | `Δseq ≥ target_fps`    |
| `/api/frame.jpg`             | HTTP 200, non-empty   |

## Common failure modes
- `connected: false` → device path wrong, container missing `/dev/video0`, RTSP URL unreachable
- `seq` flat → capture thread blocked, OpenCV missing in image (synthetic fallback should still advance)
- `frame.jpg` 503 → no frame yet (allow ~1s after start)

## Automation
```bash
ops/scripts/test_layer1_camera.sh http://localhost:8080
```
