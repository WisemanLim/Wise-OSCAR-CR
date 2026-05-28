# Layer 5 — Storage (SQLite tracks + Video recording)

## Scope
- `core/storage/Storage.{hpp,cpp}`
- SQLite WAL writer for localized tracking rows (table `tracks`)
- Segmented video recorder via OpenCV `VideoWriter` (mp4v) in `data/videos/`
- `videos` table in SQLite records segment ranges

## Goal
1. Localized track rows are persisted (rows count grows)
2. Per-object history retrievable
3. (When OpenCV present) video segments roll on schedule and are catalogued

## Preconditions
- Layers 1–4 healthy
- DB path writable: `data/oscar.sqlite` (auto-created)
- Video dir writable: `data/videos/`

## Procedure

### 5.1 Row count growth
```bash
A=$(curl -fsS http://localhost:8080/api/stats | jq -r .storage.rows)
sleep 3
B=$(curl -fsS http://localhost:8080/api/stats | jq -r .storage.rows)
echo "rows: $A -> $B"
```
Expected: `B > A` (grows ~ FPS × active-objects).

### 5.2 Recent rows
```bash
curl -fsS 'http://localhost:8080/api/tracks?limit=5' | jq '.[0]'
```
Expected fields: `ts_ms, object_id, category, state, class_name, score, bbox[4], rel_pos[3], abs_pos[3]`.

### 5.3 Per-object history
```bash
ID=$(curl -fsS 'http://localhost:8080/api/tracks?limit=1' | jq -r '.[0].object_id')
curl -fsS "http://localhost:8080/api/tracks/$ID" | jq '. | length'
```
Expected: ≥ 1 (history rows for that ID).

### 5.4 Direct SQL audit (host or `docker exec`)
```bash
docker exec -it oscar-core sqlite3 /opt/oscar/data/oscar.sqlite \
  "SELECT object_id, COUNT(*), MIN(ts_ms), MAX(ts_ms) FROM tracks GROUP BY object_id LIMIT 10;"
```

### 5.5 Video segment rolled
Wait `storage.segment_seconds + 5` (default 65s), then:
```bash
docker exec -it oscar-core ls -la /opt/oscar/data/videos/
docker exec -it oscar-core sqlite3 /opt/oscar/data/oscar.sqlite "SELECT id, path, frame_count FROM videos;"
```
Expected: ≥ 1 `oscar_YYYYMMDD_HHMMSS.mp4` + matching `videos` row.

## Pass criteria
| Check                          | Expected      |
|--------------------------------|---------------|
| `storage.rows` increasing      | yes           |
| `/api/tracks` returns shape    | yes           |
| `/api/tracks/:id` returns rows | yes           |
| `videos/` segment exists       | yes (OpenCV)  |

## Failure modes
- Rows flat → Localizer not pushing or DB path read-only
- "video disabled (no OpenCV)" log → image built with `OSCAR_USE_OPENCV=OFF`
- WAL contention → use `PRAGMA journal_mode` is already `WAL`; check disk space

## Automation
```bash
ops/scripts/test_layer5_storage.sh http://localhost:8080
```
