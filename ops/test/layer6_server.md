# Layer 6 — Server (HTTP + Server-Sent Events)

## Scope
- `core/server/Server.{hpp,cpp}` — cpp-httplib REST + SSE
- Endpoints:
  - `GET  /api/health`
  - `GET  /api/stats`
  - `GET  /api/pose`
  - `POST /api/pose`
  - `GET  /api/tracks?limit=N`
  - `GET  /api/tracks/:id`
  - `GET  /api/live`
  - `GET  /api/events`         (SSE stream of live tracks)
  - `GET  /api/frame.jpg`      (latest frame as JPEG)
  - `GET  /` (static web root, if `web/dist` present)

## Goal
Confirm the API surface and the SSE stream work end-to-end with proper CORS and chunked transfer.

## Procedure

### 6.1 Health
```bash
curl -fsS http://localhost:8080/api/health
```
Expected: `{"ok":true,"ts_ms":...,"camera_connected":true|false}`

### 6.2 Stats snapshot
```bash
curl -fsS http://localhost:8080/api/stats | jq
```

### 6.3 CORS headers
```bash
curl -fsS -D - http://localhost:8080/api/health -o /dev/null | grep -i 'access-control'
```
Expected:
```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET,POST,OPTIONS
```

### 6.4 OPTIONS preflight
```bash
curl -fsS -i -X OPTIONS http://localhost:8080/api/pose
```
Expected: HTTP `204`.

### 6.5 SSE stream
```bash
curl -N --max-time 5 http://localhost:8080/api/events | head -n 8
```
Expected: lines like `data: [...]` or heartbeat comments `: ping`.

### 6.6 JPEG snapshot
```bash
curl -fsS http://localhost:8080/api/frame.jpg -o /tmp/x.jpg
file /tmp/x.jpg
```
Expected: `JPEG image data, ...`

### 6.7 Static web root
With `oscar-web` container running:
```bash
curl -fsS http://localhost:5173/ | grep -o '<title>[^<]*'
```
Expected: `<title>OSCAR`

## Pass criteria
| Check                         | Expected      |
|-------------------------------|---------------|
| `/api/health` 200             | yes           |
| CORS headers present          | yes           |
| `/api/events` streams data    | yes           |
| `/api/frame.jpg` returns JPEG | yes           |

## Failure modes
- 503 on `/api/frame.jpg` → no frame in cache yet (allow 1–2s)
- Empty SSE → server thread joined; check container logs for `HTTP loop exit`
- Static web 404 → `cfg.server.web_root` doesn't exist (build the web app)

## Automation
```bash
ops/scripts/test_layer6_server.sh http://localhost:8080
```
