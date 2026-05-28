# Layer 2 — Object Detection (Inference)

## Scope
- `core/inference/Detector.{hpp,cpp}`
- Backends:
  - `OnnxBackend` (real YOLO via ONNX Runtime) — enabled when `OSCAR_USE_ONNX=ON` and `model_path` exists
  - `StubBackend` — emits synthetic detections (used when no model or `inference.stub_mode=true`)
- `pipe.frames` → `pipe.detections`

## Goal
Verify detector pulls frames, runs inference (real or stub), and pushes detections at acceptable latency.

## Preconditions
- Layer 1 producing frames
- `config/oscar.json` → `inference` block valid
- If running real YOLO: `models/yolo.onnx` exists and image rebuilt with `OSCAR_USE_ONNX=ON`

## Procedure

### 2.1 Inference counter advances
```bash
A=$(curl -fsS http://localhost:8080/api/stats | jq -r .inference.count)
sleep 2
B=$(curl -fsS http://localhost:8080/api/stats | jq -r .inference.count)
echo "infer: $A -> $B"
```
Expected: `B > A`. Stub backend should track the camera FPS closely.

### 2.2 Latency budget
```bash
curl -fsS http://localhost:8080/api/stats | jq .inference.latency_ms
```
Expected (per backend):
| Backend     | Typical          |
|-------------|------------------|
| Stub        | < 1 ms           |
| YOLO ONNX (CPU) | 30–80 ms     |
| YOLO ONNX (CUDA)| 5–15 ms      |

### 2.3 Detections reach tracker
Inspect live tracks (tracker output) to confirm detector is feeding it:
```bash
curl -fsS http://localhost:8080/api/live | jq '.[].class_name' | sort -u
```
Expected: at least one class name (`person`, `object`, …).

## Pass criteria
| Check                            | Expected         |
|----------------------------------|------------------|
| `inference.count` increasing     | yes              |
| `inference.latency_ms`           | < 100 ms (CPU)   |
| `/api/live` non-empty            | yes              |

## Failure modes
- `count` stagnant: `pipe.frames` empty → camera issue, not inference
- Latency very high: model running CPU-only, or container CPU starved
- ONNX init fail: logs show "ONNX init failed: ... -> stub" — stub will still pass smoke

## Automation
```bash
ops/scripts/test_layer2_inference.sh http://localhost:8080
```
