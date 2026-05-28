# End-to-End — Latency & Data Freshness

## Scope
Whole pipeline: **Camera → Inference → Tracking → Localization → Storage / Server** with a focus on the **< 500 ms** latency budget (PRD §6).

## Goal
1. Verify the wall-clock delay between a frame being captured and its localized track being readable via HTTP is below budget
2. Verify the web UI live view + bbox overlay react within ~150 ms
3. Verify recovery: stop the camera, restart, pipeline resumes without restart

## Preconditions
- All containers up (`make up`)
- Optional: `oscar-web` running for visual confirmation at `http://localhost:5173/`

## Procedure

### E2E.1 Hot-path latency
```bash
# 'ts_ms' on a live object is when the localizer emitted it.
NOW=$(date +%s%3N)
TS=$(curl -fsS http://localhost:8080/api/live | jq -r '.[0].ts_ms // 0')
echo "delta = $((NOW - TS)) ms"
```
Pass when delta < 500 ms (budget) under nominal load.

### E2E.2 Burst load — sustained latency
For 60 seconds, sample latency every 1s and record p50/p95:
```bash
for i in $(seq 1 60); do
  NOW=$(date +%s%3N)
  TS=$(curl -fsS http://localhost:8080/api/live | jq -r '.[0].ts_ms // 0')
  echo $((NOW - TS))
  sleep 1
done | tee /tmp/lat.txt
sort -n /tmp/lat.txt | awk 'NR==30{print "p50="$1}NR==57{print "p95="$1}END{print "max="$1}'
```
Pass when `p95 < 500 ms`.

### E2E.3 Pose-coupled output
```bash
curl -X POST http://localhost:8080/api/pose -H 'Content-Type: application/json' \
  -d '{"lat":37.0,"lon":127.0,"yaw":45,"pitch":-5,"roll":0}'
sleep 0.5
curl -fsS http://localhost:8080/api/live | jq '.[0].abs_pos'
```
Pass when `abs_pos` reflects the latest pose (not zero).

### E2E.4 Recovery
```bash
docker pause oscar-core   # simulate camera/stream stall
sleep 3
docker unpause oscar-core
sleep 2
curl -fsS http://localhost:8080/api/stats | jq .camera
```
Pass when `camera.connected == true` resumes within 5 s.

### E2E.5 Web UI sanity
1. Open `http://localhost:5173/`
2. Confirm the live frame refreshes ~10 Hz, KPIs animate, and bbox overlays drift with detected objects
3. Click "Use Browser GNSS" → verify `/api/pose` now matches your browser geolocation

## Pass criteria
| Check                           | Budget  |
|---------------------------------|---------|
| Hot-path single sample          | < 500 ms |
| 60-s sustained `p95`            | < 500 ms |
| Pose coupling propagates        | < 1 s    |
| Pause/unpause recovery          | < 5 s    |
| Web UI overlay smoothness       | subjective, no jitter |

## Failure modes
- Latency > 500 ms persistent → check `/api/stats.inference.latency_ms`. If CPU-bound, switch to GPU build or reduce `inference.input_size`.
- Latency spikes on every `segment_seconds` boundary → segment roll is blocking writes; consider reducing segment length or moving recording to its own disk.
- abs_pos still zero after pose POST → confirm `pose_known: true` in `/api/stats`.

## Automation
```bash
ops/scripts/test_e2e.sh http://localhost:8080
```
