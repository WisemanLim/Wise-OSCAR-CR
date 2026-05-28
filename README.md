# OSCAR — Open-source Spatial Camera tracking and Autonomous Recognition

**아키텍처: 조합 C — C++ Core Engine + React Web GUI (극단적 지연 차단 구조)**
**Architecture: 조합 C — C++ Core Engine + React Web GUI (extreme-latency-blocking design)**

**[한국어]**
OSCAR는 저지연 크로스 플랫폼 파이프라인입니다:

1. 카메라 연결 감지 및 모니터링 (USB / RTSP / 내장 카메라)
2. 매 프레임 실시간 객체 검출
3. 객체별로 안정적인 ID를 유지하는 다중 객체 추적
4. 카메라 자세 기반 위치 추정 (상대 + 절대 좌표)
5. 추적된 객체의 SQLite 영구 저장 + 분할 MP4 녹화
6. C++ HTTP/SSE API와 이를 소비하는 React/Vite 대시보드

전체 제품 요구사항은 [`.doc/OSCAR_PRD.md`](.doc/OSCAR_PRD.md)에 있습니다. 본 README는 빌드, 실행, 검증에 초점을 둡니다.

**[English]**
OSCAR is a low-latency, cross-platform pipeline for:

1. Detecting and monitoring camera connectivity (USB / RTSP / built-in)
2. Real-time object detection on each frame
3. Multi-object tracking with stable per-object IDs
4. Camera-pose-aware localization (relative + absolute coordinates)
5. SQLite persistence of localized tracks + segmented MP4 recording
6. A C++ HTTP/SSE API consumed by a React/Vite dashboard

The full product requirements are in [`.doc/OSCAR_PRD.md`](.doc/OSCAR_PRD.md). This README focuses on building, running, and verifying the implementation.

---

## 1. 저장소 구조 / Repository layout

**[한국어]** 디렉터리는 책임 단위로 분리되어 있습니다. C++ 엔진은 `core/`, 웹 대시보드는 `web/`, 런타임 설정은 `config/`, 모델/라벨은 `models/`, 모든 운영 스크립트와 Docker 자산은 `ops/`에 있습니다.

**[English]** Layout is split by responsibility. C++ engine lives in `core/`, web dashboard in `web/`, runtime config in `config/`, models/labels in `models/`, all ops scripts and Docker assets in `ops/`.

```
Wise-OSCAR-CR/
├── core/                       # C++20 engine (single executable: oscar_core)
│   ├── common/                 #   Types, Logger, Config, Pipeline, BoundedQueue
│   ├── camera/                 # L1  Camera capture (OpenCV / synthetic fallback)
│   ├── inference/              # L2  Detector (OpenCV DNN / ONNX YOLO / stub)
│   ├── tracking/               # L3  IoU + ID-lifecycle tracker
│   ├── localization/           # L4  Pose store + coordinate transform
│   ├── storage/                # L5  SQLite tracks + segmented MP4 recorder
│   ├── server/                 # L6  cpp-httplib REST + SSE
│   ├── main.cpp                #     Composition root / signal handling
│   └── CMakeLists.txt          #     Build (auto-fetches httplib + nlohmann/json)
├── web/                        # React + Vite + TypeScript dashboard
│   ├── src/
│   │   ├── components/         #   LivePanel, StatsPanel, TracksPanel, PosePanel
│   │   ├── api/oscar.ts        #   Typed REST + SSE client
│   │   ├── App.tsx, main.tsx, styles.css
│   ├── index.html, vite.config.ts, tsconfig*.json, package.json
├── config/
│   └── oscar.json              # Runtime configuration (camera, model, server, etc.)
├── models/
│   └── coco.names              # Class labels (drop a yolo.onnx alongside to use real model)
├── data/                       # Created at runtime (sqlite DB + mp4 segments)
├── ops/
│   ├── docker/
│   │   ├── Dockerfile.core     # Multi-stage build for oscar_core
│   │   ├── Dockerfile.web      # Vite build → nginx serve
│   │   └── nginx.conf          # Proxies /api → oscar-core
│   ├── docker-compose.yml      # oscar-core + oscar-web
│   ├── Makefile                # All ops + tests
│   ├── .env.example            # Runtime env overrides (copy to .env)
│   ├── scripts/                # up_local.sh + per-layer curl-based smoke tests
│   └── test/                   # Test scenario docs (one md per layer)
├── .doc/OSCAR_PRD.md
├── .gitignore
└── README.md (this file)
```

---

## 2. 계층 아키텍처 / Layered architecture

**[한국어]** 파이프라인은 6개 계층으로 구성되며, 모든 계층 간 채널은 `oscar::BoundedQueue<T>` (`core/common/Queue.hpp`)로 **drop-oldest** 의미를 가집니다 — 백프레셔가 쌓이지 않습니다. 전체 토폴로지는 [`core/main.cpp`](core/main.cpp)에서 조립됩니다.

**[English]** Six-layer pipeline. Inter-layer channels are `oscar::BoundedQueue<T>` (`core/common/Queue.hpp`) with **drop-oldest** semantics — back-pressure can't pile up. The whole topology is composed in [`core/main.cpp`](core/main.cpp).

| # | Layer        | Directory                | Inputs                   | Outputs (`pipe.*` channel)        |
|---|--------------|--------------------------|--------------------------|-----------------------------------|
| 1 | Camera       | `core/camera/`           | OS device / RTSP URL     | `frames`, `frames_rec`, `frames_ws` |
| 2 | Inference    | `core/inference/`        | `frames`                 | `detections`                      |
| 3 | Tracking     | `core/tracking/`         | `detections`             | `tracks`                          |
| 4 | Localization | `core/localization/`     | `tracks` + pose store    | `localized`, `localized_srv`      |
| 5 | Storage      | `core/storage/`          | `localized`, `frames_rec`| SQLite rows + MP4 segments        |
| 6 | Server       | `core/server/`           | `localized_srv`, `frames_ws`, Storage queries | HTTP/SSE        |
| W | Web GUI      | `web/`                   | HTTP/SSE                 | Browser dashboard                 |

```
            ┌────────────┐   frames   ┌──────────────┐  detections  ┌────────────┐
RTSP/USB ──▶│  Camera L1 │──────────▶│ Inference L2 │────────────▶│ Tracking L3│
            └─────┬──────┘           └──────────────┘             └──────┬─────┘
                  │                                                       │ tracks
                  │ frames_rec, frames_ws                                  ▼
                  │                                ┌─────────────────────────────┐
                  │                                │       Localization L4       │
                  │                                │  PoseStore + coord-transform │
                  │                                └──────┬───────────────┬──────┘
                  ▼                                       │ localized     │ localized_srv
            ┌────────────┐  frames_rec     ┌──────────────▼───┐    ┌──────▼──────────┐
            │ Storage L5 │◀──────────────  │ tracks table      │    │ Server L6       │
            │ (SQLite +  │                 │ + videos table    │    │ HTTP/SSE        │
            │  ffmpeg)   │                 └────────────────────┘    │  + static web   │
            └────────────┘                                            └─────────────────┘
                                                                            │
                                                                  ┌─────────▼────────┐
                                                                  │  Web GUI (React) │
                                                                  └──────────────────┘
```

---

## 3. 빠른 시작 — Docker compose / Quick start — Docker compose

**[한국어]** 두 이미지를 빌드하고 detached 모드로 실행합니다. 처음에는 인터넷이 필요합니다 (FetchContent + apt + npm).

**[English]** Builds both images and runs them detached. First run needs internet (FetchContent + apt + npm).

```bash
cd ops
make build          # docker compose build
make up             # detached
# oscar-core: http://localhost:8080   (REST + SSE + JPEG snapshot)
# oscar-web : http://localhost:5173/  (UI; /api/* proxied to core)
make logs           # tail logs
make test           # run all layer scripts (curl-driven)
make down
```

**[한국어]** 호스트 웹캠을 `oscar-core`에 노출하려면 (**Linux 호스트 전용**), [ops/docker-compose.yml](ops/docker-compose.yml)의 `devices:` 블록 (3줄 — `devices:` 키와 `- "/dev/video0:/dev/video0"` 매핑) 주석을 해제하고, `config/oscar.json → camera.source = "/dev/video0"`로 설정하거나 UI에서 런타임에 전환 (§3.5).

**[English]** To expose a host webcam to `oscar-core` (**Linux host only**), uncomment the `devices:` block in [ops/docker-compose.yml](ops/docker-compose.yml) (three lines — the `devices:` key plus the `- "/dev/video0:/dev/video0"` mapping) and either set `config/oscar.json → camera.source = "/dev/video0"` or switch at runtime from the UI (see §3.5).

> **[한국어] macOS / Windows 호스트:** Docker Desktop은 Linux VM에서 컨테이너를 실행하며 호스트 USB / AVFoundation / DirectShow 카메라를 패스스루할 수 **없습니다**. `make up-local` (§3.2)을 사용하거나, 폰에서 RTSP로 스트리밍 (§3.3) 후 URL을 picker (§3.5)나 `ops/.env` (§3.1)를 통해 적용하세요.
>
> **[English] macOS / Windows hosts:** Docker Desktop runs containers in a Linux VM and **cannot pass host USB / AVFoundation / DirectShow cameras through**. Use `make up-local` (§3.2), or stream from a phone via RTSP (§3.3) and apply the URL through the picker (§3.5) or `ops/.env` (§3.1).

**[한국어]** RTSP는 `camera.source`를 스트림 URL로 설정합니다 (UI에서 전환 시 재빌드 불필요):

**[English]** For RTSP, set `camera.source` to the stream URL (no rebuild required when switching from the UI):

```json
"camera": { "source": "rtsp://user:pass@10.0.0.5:554/stream", "target_fps": 25 }
```

### 3.1 ops/.env로 런타임 오버라이드 / Runtime overrides via `ops/.env`

**[한국어]** Compose가 `ops/.env`를 자동 로드합니다. 다음 `OSCAR_*` 변수가 `config/oscar.json` 값을 덮어씁니다 ([ops/.env.example](ops/.env.example) 참조):

**[English]** Compose auto-loads `ops/.env` for the following `OSCAR_*` overrides (see [ops/.env.example](ops/.env.example)):

| 변수 / Variable         | 효과 / Effect (in core)                                              |
|-------------------------|----------------------------------------------------------------------|
| `OSCAR_CAMERA_SOURCE`   | `camera.source`를 대체 / Replaces `camera.source` from `config/oscar.json` |
| `OSCAR_INFERENCE_STUB`  | `0`/`false`이면 실제 모델 로드 강제; 그 외는 stub / `0`/`false` forces real model load; anything else = stub |
| `OSCAR_INFERENCE_MODEL` | `inference.model_path`를 대체 / Replaces `inference.model_path`      |
| `OSCAR_HTTP_PORT`       | `server.port`를 대체 / Replaces `server.port`                        |
| `OSCAR_HTTP_BIND`       | `server.bind`를 대체 / Replaces `server.bind`                        |

```bash
cd ops
make env-init                 # .env.example -> .env 복사 (덮어쓰기 안 함 / no overwrite)
$EDITOR .env                  # set OSCAR_CAMERA_SOURCE=rtsp://...
make up                       # .env 자동 사용 / uses .env automatically
# 또는 OSCAR_CAMERA_SOURCE가 설정되었는지 강제 검사:
# or, with a hard check that OSCAR_CAMERA_SOURCE is set:
make up-rtsp
```

### 3.2 macOS / Windows 네이티브 — `make up-local` / macOS / Windows native — `make up-local`

**[한국어]** Docker가 카메라에 접근할 수 없는 호스트(macOS, Windows)에서는, 한 명령으로 core를 네이티브 빌드하고 `oscar_core`와 vite dev 서버를 모두 실행하며 Ctrl+C로 동시에 종료됩니다:

**[English]** On hosts where Docker can't access cameras (macOS, Windows), one command builds the core natively, starts both `oscar_core` and the vite dev server, and shuts both down on Ctrl+C:

```bash
brew install cmake opencv     # 한 번만 (macOS); Linux는 apt 사용 — §4.1 참조
cd ops
make up-local
#   oscar-core: http://localhost:8080
#   oscar-web : http://localhost:5173   (HMR; proxies /api to core)
```

**[한국어]** 스크립트([ops/scripts/up_local.sh](ops/scripts/up_local.sh))는 `ops/.env`를 존중하므로 `OSCAR_CAMERA_SOURCE`를 설정해 RTSP URL을 `config/oscar.json` 수정 없이 사용할 수 있습니다.

**[English]** The script ([ops/scripts/up_local.sh](ops/scripts/up_local.sh)) honours `ops/.env` too — set `OSCAR_CAMERA_SOURCE` there to pick an RTSP URL without editing `config/oscar.json`.

### 3.3 폰을 카메라로 사용하기 (RTSP) / Using a phone as the camera (RTSP)

**[한국어]** macOS / Windows에서 Docker Desktop의 USB 패스스루 불가를 우회하는 가장 좋은 방법. 폰에 맞는 경로를 선택하세요.

**[English]** Best way around Docker Desktop's no-USB-passthrough on macOS / Windows. Pick the path for your phone.

> **[한국어] 모든 경로 공통 전제:**
> - 폰과 OSCAR 호스트가 **같은 Wi-Fi**에 있어야 합니다 (모바일 데이터 불가).
> - 폰 IP를 브로드캐스터 앱에서 확인 후 호스트에서 `ping <PHONE_IP>`으로 도달 확인.
> - **호스트 자신의 IP를 사용하지 마세요**: `ifconfig | grep "inet 192"`로 호스트 IP를 확인합니다 — `OSCAR_CAMERA_SOURCE`가 그 IP와 같으면 OSCAR가 자기 자신으로 루프백되어 `Could not read from resource` / `Parse error`가 발생합니다.
>
> **[English] Common preconditions for every path below:**
> - Phone and OSCAR host on the **same Wi-Fi** (mobile data won't work).
> - Find phone IP from the broadcaster app, then `ping <PHONE_IP>` from the host to confirm reach.
> - **Never type the host's own IP**: `ifconfig | grep "inet 192"` to learn what the host is — if `OSCAR_CAMERA_SOURCE` IP matches that, OSCAR loops back to itself and you get `Could not read from resource` / `Parse error`.

#### 3.3.1 iPhone — 가장 쉬운 경로: **Continuity Camera** (macOS 13 Ventura 이상) / easiest path: **Continuity Camera** (macOS 13 Ventura or newer)

**[한국어]** 앱, 네트워크, 추가 컨테이너 모두 불필요. iPhone이 macOS의 일반 USB 카메라로 표시되며 OSCAR enumerator가 `iPhone 카메라`/`iPhone Camera`로 이미 나열합니다.

**[English]** No app, no network, no extra container. The iPhone shows up as a regular USB camera to macOS via Continuity Camera. OSCAR's enumerator already lists it as `iPhone 카메라`/`iPhone Camera`.

1. **[한국어]** iPhone: **설정 → 일반 → AirPlay 및 Handoff → 연속성 카메라 = 켬**. Mac과 동일한 Apple ID로 로그인.
   **[English]** iPhone: **Settings → General → AirPlay & Handoff → Continuity Camera = On**. Sign in with the same Apple ID as the Mac.
2. **[한국어]** iPhone을 거치 (추적할 대상을 향해 후면 카메라). 잠금을 한 번 해제하면 macOS가 자동 페어링.
   **[English]** Mount the iPhone (rear cam facing what you want to track). Unlock it once; macOS auto-pairs.
3. **[한국어]** Mac에서: `make -C ops up-local` (§3.2) — 처음 한 번 `brew install opencv` 필요.
   **[English]** On the Mac: `make -C ops up-local` (§3.2) — needs `brew install opencv` once.
4. **[한국어]** OSCAR UI: *Stats → CAMERA SOURCE* → `iPhone 카메라 (#1)` 선택 → **Apply**.
   **[English]** In the OSCAR UI: *Stats → CAMERA SOURCE* → pick `iPhone 카메라 (#1)` → **Apply**.

**[한국어] 주의:** Continuity Camera는 Mac과 iPhone이 같은 Apple ID이고 물리적으로 가까이 있어야 합니다. Docker 컨테이너에서는 동작하지 않으므로 `make up-local`이 필요합니다.

**[English] Caveat:** Continuity Camera requires the Mac and iPhone in the same Apple ID and physically near each other. It does *not* work from a Docker container — that's why you need `make up-local`.

#### 3.3.2 iPhone — Docker 호환 경로: **Larix Broadcaster → MediaMTX 릴레이** / Docker-compatible path: **Larix Broadcaster → MediaMTX relay**

**[한국어]** OSCAR를 macOS에서 Docker로 실행해야 하거나 (또는 Mac이 Ventura보다 오래된 경우), Larix로 iPhone에서 로컬 RTSP 릴레이로 푸시하고 OSCAR가 릴레이에서 풀합니다.

**[English]** If you must run OSCAR in Docker on macOS (or your Mac is older than Ventura), use Larix to push from iPhone to a local RTSP relay; OSCAR pulls from the relay.

1. **[한국어] Mac 호스트** — 릴레이 시작 (우리가 관리하는 `bluenviron/mediamtx` 컨테이너):
   **[English] Mac host** — start the relay (a `bluenviron/mediamtx` container we manage for you):
   ```bash
   make -C ops rtsp-server
   # 포트: 8554/rtsp, 1935/rtmp, 8888/hls (브라우저 미리보기)
   # listens on 8554/rtsp, 1935/rtmp, 8888/hls (browser preview)
   ```

2. **[한국어] iPhone** — **Larix Broadcaster** 설치 (무료, App Store).
   **[English] iPhone** — install **Larix Broadcaster** (free, App Store).
   - **[한국어]** 설정 → **Connections** → **+** → 새 연결
     - URL: `rtsp://<MAC_IP>:8554/iphone` *(Mac의 en0 LAN IP, 전제 조건 참조)*
     - Mode: `Publish`
   - **[English]** Settings → **Connections** → **+** → New connection
     - URL: `rtsp://<MAC_IP>:8554/iphone` *(use the Mac's en0 LAN IP, see preconditions)*
     - Mode: `Publish`
   - **[한국어]** 뒤로 돌아가 빨간 녹화 버튼 탭. 푸시 라이브 상태일 때 `Connected` 표시.
     **[English]** Back, then tap the red record button. The app shows `Connected` when push is live.
   - **[한국어]** Mac 브라우저에서 확인: `http://localhost:8888/iphone`에서 영상 표시되어야 함.
     **[English]** Verify in a browser on the Mac: `http://localhost:8888/iphone` should show video.

3. **[한국어] Mac 호스트** — OSCAR를 릴레이로 지정:
   **[English] Mac host** — point OSCAR at the relay:
   ```bash
   # ops/.env
   OSCAR_CAMERA_SOURCE=rtsp://host.docker.internal:8554/iphone
   ```
   **[한국어]** `host.docker.internal`은 Docker 컨테이너가 Mac 호스트를 통해 릴레이 컨테이너에 도달하게 해줍니다.
   **[English]** `host.docker.internal` lets the Docker container reach the relay container via the Mac host.
   ```bash
   make -C ops up-rtsp
   ```

4. **[한국어]** 종료 시: `make -C ops down && make -C ops rtsp-server-stop`.
   **[English]** Teardown when done: `make -C ops down && make -C ops rtsp-server-stop`.

#### 3.3.3 Android — **IP Webcam** (무료, 가장 단순 / free, simplest)

1. **[한국어]** Play Store에서 **IP Webcam** (Pavel Khlebovich) 설치.
   **[English]** Install **IP Webcam** (Pavel Khlebovich) from Play Store.
2. **[한국어]** 앱 실행 → 하단 **Start server**. 폰 화면에 `IPv4: 192.168.0.42:8080` 같은 주소가 표시됩니다. 이 IP는 **폰**이지 Mac이 아닙니다.
   **[English]** Open it → scroll to **Start server**. Phone screen shows e.g. `IPv4: 192.168.0.42:8080`. The IP shown there is the phone, **not** the Mac.
3. **[한국어]** RTSP H.264 endpoint: `rtsp://<PHONE_IP>:8080/h264_ulaw.sdp`. HTTP MJPEG fallback: `http://<PHONE_IP>:8080/video`.
   **[English]** RTSP H.264 endpoint: `rtsp://<PHONE_IP>:8080/h264_ulaw.sdp`. MJPEG-over-HTTP fallback: `http://<PHONE_IP>:8080/video`.
4. **[한국어]** 다음 중 하나로 적용:
   - **UI 한 번에** — *Stats → CAMERA SOURCE → Custom URL → Apply URL* (§3.5).
   - **`ops/.env`** —
     ```bash
     echo 'OSCAR_CAMERA_SOURCE=rtsp://<PHONE_IP>:8080/h264_ulaw.sdp' >> ops/.env
     make -C ops up-rtsp
     ```
   - **정적** — `config/oscar.json`의 `camera.source` 설정 후 재빌드.

   **[English]** Apply via any of:
   - **UI one-shot** — *Stats → CAMERA SOURCE → Custom URL → Apply URL* (§3.5).
   - **`ops/.env`** —
     ```bash
     echo 'OSCAR_CAMERA_SOURCE=rtsp://<PHONE_IP>:8080/h264_ulaw.sdp' >> ops/.env
     make -C ops up-rtsp
     ```
   - **Static** — set `camera.source` in `config/oscar.json` and rebuild.

> **[한국어]** IP Webcam 설정에서 Wi-Fi sleep을 비활성화하세요. 화면이 꺼지는 순간 스트림이 끊깁니다.
>
> **[English]** Disable Wi-Fi sleep in IP Webcam's settings, or the stream drops the moment the screen turns off.

#### 3.3.4 Android — Larix Broadcaster (iPhone §3.3.2와 동일 / same flow as iPhone §3.3.2)

**[한국어]** IP Webcam의 H.264 RTSP가 기기에서 불안정하면 §3.3.2와 똑같이 Larix → MediaMTX를 사용. 1, 3, 4단계는 동일하며, 2단계만 Android Larix 앱을 사용.

**[English]** If IP Webcam's H.264 RTSP isn't reliable on your device, use Larix → MediaMTX exactly as in §3.3.2. Steps 1, 3, and 4 are identical; step 2 uses the Android Larix app.

#### 3.3.5 RTSP 빠른 트러블슈팅 / Quick troubleshooting (RTSP)

| `make logs` / `docker logs oscar-core` 증상 / Symptom            | 해결 / Fix                                                                                                                       |
|------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------|
| `Could not read from resource` / `Parse error`                   | URL이 폰/릴레이가 아닌 Mac 자체를 가리킴. IP 재확인. / URL points at the Mac itself, not the phone/relay. Re-check IPs.            |
| `uridecodebin` / `can't find starting number`                    | GStreamer 플러그인 없는 구버전 core 이미지. `make build`로 재빌드. / Old core image without GStreamer plugins. `make build` to rebuild. |
| `Connection refused` to `host.docker.internal`                   | 릴레이 미실행. `make rtsp-server` 먼저. Linux 호스트는 `<MAC_IP>` 사용. / Relay not running. `make rtsp-server` first. On Linux hosts use the relay's `<MAC_IP>` instead. |
| Larix shows `Disconnected` immediately                           | Mac IP 오류, 8554 차단된 방화벽, 또는 라우터 Wi-Fi isolation. / Wrong Mac IP, firewall blocks 8554, or Wi-Fi isolation enabled.   |
| `make rtsp-server` says "already running"                        | `make rtsp-server-stop` 후 재시도. / `make rtsp-server-stop` then retry.                                                          |

### 3.4 실제 객체 검출 (YOLO) / Real object detection (YOLO)

**[한국어]** 기본적으로 `oscar-core`는 **stub 추론 백엔드**로 실행됩니다 — 실제 카메라 프레임에 가짜 객체를 만들지 않으므로 박스 없이 영상만 표시됩니다. 세 명령으로 실제 YOLO 검출을 활성화:

**[English]** By default `oscar-core` runs the **stub inference backend** — it does *not* invent objects on real camera frames, so the Live view will show video without any boxes. To get real YOLO detections in three commands:

```bash
make -C ops download-model            # 1회; models/에 yolov8n.onnx 내보내기
make -C ops env-init                  # .env가 없으면
sed -i.bak 's/^OSCAR_INFERENCE_STUB=.*$/OSCAR_INFERENCE_STUB=0/' ops/.env
make -C ops restart
```

**[한국어]** `models/`는 컨테이너에 볼륨 마운트되어 있어, 이후 모델 교체(`yolov8s.onnx`, 커스텀 학습 모델 등)는 다음 `make restart`로 즉시 반영됩니다 — 이미지 재빌드 불필요. 자세한 백엔드 설명은 §4.5 참조.

**[English]** `models/` is mounted into the container, so any subsequent model swap (e.g. `yolov8s.onnx`, custom-trained) takes effect on the next `make restart` with no image rebuild. Full backend details in §4.5.

### 3.5 카메라 picker (런타임) / Camera picker (runtime)

**[한국어]** 웹 대시보드의 **Stats → CAMERA SOURCE** 섹션이 호스트 카메라를 열거하고 `oscar_core` 재시작 없이 소스를 전환합니다:

**[English]** The web dashboard's **Stats → CAMERA SOURCE** section enumerates the host's cameras and lets you switch sources without restarting `oscar_core`:

- **[한국어] Refresh** — 호스트 enumeration 재실행 (`/dev/video*` on Linux, `system_profiler SPCameraDataType` on macOS, OpenCV index probe fallback).
  **[English] Refresh** — re-runs host enumeration (`/dev/video*` on Linux, `system_profiler SPCameraDataType` on macOS, OpenCV index probe as fallback).
- **[한국어] Dropdown + Apply** — 열거된 장치 선택 후 `/api/camera/source`로 POST; 카메라 스레드가 새 소스로 재열림.
  **[English] Dropdown + Apply** — selects an enumerated device and POSTs to `/api/camera/source`; the camera thread reopens with the new source.
- **[한국어] Custom URL + Apply URL** — OpenCV가 열 수 있는 어떤 문자열도 허용: 장치 경로(`/dev/video2`), 인덱스(`1`), RTSP/HTTP 스트림, 비디오 파일.
  **[English] Custom URL + Apply URL** — accepts any string OpenCV can open: device path (`/dev/video2`), index (`1`), RTSP/HTTP stream, or a video file.
- **[한국어]** 컨트롤 아래에 플랫폼별 힌트 표시 (예: "Docker Desktop on macOS cannot pass host USB cameras…").
  **[English]** Below the controls, a platform-aware hint appears when no devices are found.

**[한국어]** 새 소스는 실행 중인 프로세스에만 유지됩니다 — 영구화하려면 `config/oscar.json`의 `camera.source` 갱신 또는 `ops/.env`의 `OSCAR_CAMERA_SOURCE` 사용.

**[English]** The new source persists for the running process only; to make it permanent, update `camera.source` in `config/oscar.json` or use `OSCAR_CAMERA_SOURCE` in `ops/.env`.

---

## 4. 로컬 빌드 (Docker 없이) / Local build (without Docker)

### 4.1 시스템 의존성 / System dependencies

**Ubuntu / Debian**
```bash
sudo apt-get install -y build-essential cmake git pkg-config \
                        libopencv-dev libsqlite3-dev libssl-dev ffmpeg \
                        gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
                        gstreamer1.0-plugins-bad gstreamer1.0-libav
```

**macOS (Homebrew)**
```bash
brew install cmake opencv sqlite ffmpeg
```

**[한국어] Windows** — 공식 [OpenCV 릴리스](https://opencv.org/releases/) + [SQLite amalgamation](https://www.sqlite.org/download.html)을 사용하고 CMake에 `-DOpenCV_DIR=...` / `-DSQLite3_INCLUDE_DIR=...` 경로 전달.

**[English] Windows** — use the official [OpenCV release](https://opencv.org/releases/) + [SQLite amalgamation](https://www.sqlite.org/download.html) and pass paths via `-DOpenCV_DIR=...` / `-DSQLite3_INCLUDE_DIR=...` to CMake.

**[한국어]** 웹 앱은 Node 20+ 가 필요합니다. / **[English]** Node 20+ is required for the web app.

### 4.2 C++ core 빌드 / Build the C++ core

```bash
cmake -S core -B build -DCMAKE_BUILD_TYPE=Release \
      -DOSCAR_USE_OPENCV=ON \
      -DOSCAR_USE_ONNX=OFF
cmake --build build -j
```

**[한국어]** cpp-httplib와 nlohmann/json은 `FetchContent`로 자동 가져옵니다 (첫 빌드 시 네트워크 필요).

**[English]** cpp-httplib and nlohmann/json are auto-fetched via `FetchContent` (first build needs network).

### 4.3 Web UI 빌드 / Build the web UI

```bash
cd web
npm install
npm run build       # -> web/dist
```

### 4.4 실행 / Run

```bash
mkdir -p data/videos
./build/oscar_core --config ./config/oscar.json
```

**[한국어]** `http://localhost:8080/` 열기 (core가 `web/dist`를 정적 루트로 서빙). HMR 개발용:

**[English]** Open `http://localhost:8080/` (core serves `web/dist` as the static root). For HMR development:

```bash
cd web
npm run dev         # http://localhost:5173, proxies /api to :8080
```

### 4.5 실제 YOLO 검출 활성화 / Enable real YOLO detection

**[한국어]** 두 가지 실제 디텍터 백엔드가 있으며 빌드에 맞는 것을 선택합니다.

**[English]** There are two real-detector backends; pick the one that matches your build.

#### 4.5.1 OpenCV DNN (추천 — 추가 의존성 없음 / recommended — no extra dependency)

**[한국어]** `oscar-core`는 이미 OpenCV를 링크하며, DNN 모듈이 YOLOv5/v8 ONNX 파일을 직접 로드합니다. ONNX Runtime 설치 불필요. v5 (`[1, N, 85]`)와 v8 (`[1, 84, N]`) 출력 레이아웃 자동 감지.

**[English]** `oscar-core` already links OpenCV, which ships with a DNN module that loads YOLOv5/v8 ONNX files directly. No ONNX Runtime install needed. Auto-detects v5 (`[1, N, 85]`) vs v8 (`[1, 84, N]`) output layouts.

```bash
# 1. 모델 가져오기 (Ultralytics docker로 1회 — models/yolo.onnx로 내보냄)
make -C ops download-model

# 2. .env에서 stub 끄기
make -C ops env-init
$EDITOR ops/.env                          # set: OSCAR_INFERENCE_STUB=0
make -C ops restart                       # or: make up-rtsp / up-local

# 3. 확인
curl http://localhost:8080/api/stats      # inference.count 증가;
                                          # inference.latency_ms ≈ 150–300 ms (CPU, 640²)
```

**[한국어]** Compose가 `models/`를 볼륨으로 마운트하므로 `models/yolo.onnx`를 다른 YOLOv5/v8 export로 교체하면 다음 `make restart`에 적용됩니다 — 이미지 재빌드 불필요.

**[English]** The compose file mounts `models/` as a volume, so swapping `models/yolo.onnx` for any other YOLOv5/v8 export takes effect on next `make restart` — no image rebuild.

**[한국어]** 설정 가능 옵션 (`config/oscar.json → inference`):
- `model_path` — ONNX 경로 (기본 `models/yolo.onnx`).
- `labels_path` — 줄바꿈 구분 클래스 이름 (기본 `models/coco.names`, 80 COCO 클래스).
- `input_size` — 정사각 네트워크 크기 (기본 `640`). 작을수록 빠르지만 mAP 저하.
- `conf_threshold`, `iou_threshold` — 신뢰도 / NMS IoU.

**[English]** Configurable knobs (in `config/oscar.json → inference`):
- `model_path` — path to ONNX (default `models/yolo.onnx`).
- `labels_path` — newline-delimited class names (default `models/coco.names`, 80 COCO classes).
- `input_size` — square network size (default `640`). Smaller is faster but worse mAP.
- `conf_threshold`, `iou_threshold` — confidence / NMS IoU.

#### 4.5.2 ONNX Runtime (선택, 빠른 provider / optional, faster providers)

**[한국어]** CUDA/CoreML/TensorRT execution provider가 필요하면 ONNX Runtime을 링크. `core/inference/Detector.cpp`의 `OnnxBackend` 스켈레톤은 최소 구현 — 모델별 pre/postprocessing을 직접 wire:

**[English]** If you want CUDA/CoreML/TensorRT execution providers, link ONNX Runtime instead. The `OnnxBackend` skeleton in `core/inference/Detector.cpp` is intentionally minimal — wire pre/postprocessing for your specific model:

```bash
export ONNXRUNTIME_DIR=/opt/onnxruntime-linux-x64-1.18.0
cmake -S core -B build -DOSCAR_USE_ONNX=ON
cmake --build build -j
```

**[한국어]** 그 후 `models/yolo.onnx`에 모델을 놓고 `OSCAR_INFERENCE_STUB=0` 설정. ONNX Runtime과 OpenCV DNN 모두 사용 가능 시, 디텍터는 먼저 ONNX 시도 후 실패 시 OpenCV DNN으로 폴백.

**[English]** Then drop a model at `models/yolo.onnx` and set `OSCAR_INFERENCE_STUB=0`. When both ONNX Runtime and OpenCV DNN are available, the detector tries ONNX first and falls back to OpenCV DNN if init fails.

> **[한국어]** Stub 백엔드는 항상 안전망으로 동작합니다. v0.1부터 **실제 카메라 프레임에 합성 박스를 만들지 않습니다** — 모델이 로드되지 않으면 라이브 뷰는 비어 있습니다 (이전의 떠다니는 `#0 person/car` 가상 박스 없음).
>
> **[English]** The stub backend always runs as a safety net. Since v0.1 it **no longer fabricates synthetic boxes on real camera frames** — if the model isn't loaded, the live view stays empty rather than showing a phantom `#0 person/car` bouncing across the screen.

---

## 5. 계층별 개별 실행 (docker compose 없이) / Per-layer individual run (no docker compose)

**[한국어]** 각 계층을 단독으로 실행할 수 있습니다. 가장 빠른 방법은 모두 켠 상태에서 API로 특정 계층만 검증하는 것입니다:

**[English]** You can run each layer in isolation by enabling only the relevant component flow. The fastest way is to leave them all live and exercise a single one via the API:

### 5.1 Camera only (Layer 1)
- **[한국어]** `"inference.stub_mode": true` + `"inference.conf_threshold": 1.0` 설정 (모든 검출 억제).
  **[English]** Set `"inference.stub_mode": true` and `"inference.conf_threshold": 1.0` (suppress all detections).
- Run `./build/oscar_core` → poll `/api/stats.camera`.
- `ops/scripts/test_layer1_camera.sh http://localhost:8080`

### 5.2 Inference (Layer 2)
- **[한국어]** Stub 모드 유지 또는 실제 ONNX 모델 연결.
  **[English]** Keep stub mode or wire a real ONNX model.
- Poll `/api/stats.inference.{count,latency_ms}`.
- `ops/scripts/test_layer2_inference.sh`

### 5.3 Tracking (Layer 3)
- **[한국어]** 시간에 따라 `/api/live` 확인 — ID가 유지되어야 함.
  **[English]** Inspect `/api/live` over time — IDs should persist.
- `ops/scripts/test_layer3_tracking.sh`

### 5.4 Localization (Layer 4)
- **[한국어]** Pose POST 후 `/api/live`에서 `rel_pos`/`abs_pos` 확인.
  **[English]** POST a pose, then read `/api/live` and confirm `rel_pos`/`abs_pos`.
- `ops/scripts/test_layer4_localization.sh`

### 5.5 Storage (Layer 5)
- **[한국어]** 잠시 대기 후 `/api/tracks?limit=N` + `data/oscar.sqlite` 직접 점검.
  **[English]** Wait, then `/api/tracks?limit=N` + inspect `data/oscar.sqlite`.
- `ops/scripts/test_layer5_storage.sh`

### 5.6 Server (Layer 6)
- `/api/health`, `/api/events`, `/api/frame.jpg`.
- `ops/scripts/test_layer6_server.sh`

**[한국어]** 모든 계층 테스트 문서는 [ops/test/](ops/test/) (계층별 md 1개 + e2e).
**[English]** All layer test docs live in [ops/test/](ops/test/) (one md per layer + e2e).

---

## 6. 설정 / Configuration

**[한국어]** [config/oscar.json](config/oscar.json)이 단일 설정 소스입니다. `--config`로 대체 경로 전달 가능.

**[English]** [config/oscar.json](config/oscar.json) is the single config source. Pass an alternate path with `--config`.

```json
{
  "camera":       { "source": "0", "target_fps": 30, "width": 1280, "height": 720, "reconnect_ms": 2000 },
  "inference":    { "model_path": "models/yolo.onnx", "labels_path": "models/coco.names",
                    "conf_threshold": 0.25, "iou_threshold": 0.45, "input_size": 640,
                    "provider": "cpu", "stub_mode": true },
  "tracker":      { "match_thresh": 0.8, "track_buffer": 30, "det_thresh": 0.5, "frame_rate": 30 },
  "localization": { "fx": 800, "fy": 800, "cx": 640, "cy": 360,
                    "ground_height_m": 0.0, "camera_height_m": 1.5, "static_velocity_thr": 0.1 },
  "storage":      { "db_path": "data/oscar.sqlite", "video_dir": "data/videos",
                    "segment_seconds": 60, "enable_video": true },
  "server":       { "bind": "0.0.0.0", "port": 8080, "ws_port": 8081, "web_root": "web/dist" }
}
```

**[한국어]** 주요 옵션:
- **camera.source** — `"0"` (첫 웹캠), `"/dev/video1"`, `rtsp://...`. 런타임에 picker (§3.5), `POST /api/camera/source` (§7), `ops/.env`의 `OSCAR_CAMERA_SOURCE` (§3.1)로 전환 가능 — JSON 값은 부팅 기본값일 뿐.
- **inference.stub_mode** — `true`면 ONNX 우회.
- **localization.{fx,fy,cx,cy}** — 카메라 intrinsics; 정확한 world coordinates를 위해 카메라별로 캘리브레이션.
- **storage.segment_seconds** — MP4 분할 길이.

**[English]** Notable knobs:
- **camera.source** — `"0"` (first webcam), `"/dev/video1"`, or `rtsp://...`. Can also be switched at runtime via the **Stats → CAMERA SOURCE** picker (§3.5), `POST /api/camera/source` (§7), or `OSCAR_CAMERA_SOURCE` in `ops/.env` (§3.1) — the file value is just the boot default.
- **inference.stub_mode** — `true` to bypass ONNX entirely
- **localization.{fx,fy,cx,cy}** — intrinsics; calibrate per camera for accurate world coordinates
- **storage.segment_seconds** — MP4 segment length

---

## 7. HTTP API 레퍼런스 / HTTP API reference

| Method | Path                  | 설명 / Description                                                |
|--------|------------------------|--------------------------------------------------------------------|
| GET    | `/api/health`          | 생존 + 카메라 플래그 / Liveness + camera flag                       |
| GET    | `/api/stats`           | 계층별 카운터 + latency / Per-layer counters + latency             |
| GET    | `/api/pose`            | 최신 카메라 자세 / Latest camera pose                              |
| POST   | `/api/pose`            | 자세 업데이트 `{lat,lon,alt,yaw,pitch,roll}` / Update pose         |
| GET    | `/api/live`            | 메모리상 최신 추적 객체 / In-memory latest tracked objects          |
| GET    | `/api/events`          | **SSE** stream — `/api/live` shape array / Server-Sent Events     |
| GET    | `/api/tracks?limit=N`  | 최근 N개 영구 저장 행 / Most recent N persisted rows                |
| GET    | `/api/tracks/:id`      | 특정 object_id 이력 / History for one object_id                   |
| GET    | `/api/frame.jpg`       | 최신 프레임 JPEG (OpenCV 없으면 BGR raw) / Latest decoded frame as JPEG |
| GET    | `/api/camera/devices`  | 호스트 카메라 + `{platform, in_container, current}` / Enumerated host cameras |
| GET    | `/api/camera/source`   | 현재 `camera.source` 문자열 / Current `camera.source` string       |
| POST   | `/api/camera/source`   | 런타임 전환 `{ "source": "0" \| "/dev/video1" \| "rtsp://..." }` / Switch camera at runtime |
| GET    | `/...`                 | 정적 `web_root` (있으면) / Static `web_root` (when present)        |

**[한국어]** 모든 엔드포인트는 `Access-Control-Allow-Origin: *`를 반환하므로 Vite dev 서버(`:5173`)가 core를 직접 호출할 수 있습니다. OPTIONS preflight는 `204` 반환.

**[English]** All endpoints return `Access-Control-Allow-Origin: *` so the Vite dev server (`:5173`) can call the core directly. OPTIONS preflights return `204`.

---

## 8. 성능 노트 (PRD §6) / Performance notes (per PRD §6)

**[한국어]**
- 모든 계층간 채널은 **drop-oldest** 의미로 bounded — 백프레셔 누적 없음.
- 카메라 캡처, 추론, 추적, 위치 추정, 저장, HTTP 각각 **별도 스레드** 실행.
- SSE stream은 track-version 변경 시에만 emit (~10 Hz). JPEG endpoint는 in-memory 프레임 캐시 서빙 (DB/디스크 hit 없음).
- 비디오 writer는 OpenCV `mp4v` fourcc 사용, `segment_seconds`마다 롤. 하드웨어 인코더 선택(NVENC, VideoToolbox, MediaCodec)은 [Storage.cpp](core/storage/Storage.cpp)의 플랫폼별 확장 포인트.
- 측정 목표 latency (프레임 capture → `/api/live`): CPU 전용 stub 실행에서 **< 500 ms**; GPU YOLO에서 **< 150 ms**. 측정은 [`ops/scripts/test_e2e.sh`](ops/scripts/test_e2e.sh).

**[English]**
- All inter-layer channels are bounded with **drop-oldest** semantics — back-pressure can't pile up.
- Camera capture, inference, tracking, localization, storage, and HTTP each run in their **own thread**.
- The SSE stream emits only on track-version change (~10 Hz), and the JPEG endpoint serves the in-memory frame cache (no DB / disk hit).
- Video writer uses OpenCV's `mp4v` fourcc and rolls on `segment_seconds`. Hardware encoder selection (NVENC, VideoToolbox, MediaCodec) is left as a per-platform extension point in [Storage.cpp](core/storage/Storage.cpp).
- Tested-target latency budget (frame capture → `/api/live`): **< 500 ms** on a CPU-only stub run; **< 150 ms** with GPU YOLO. Use [`ops/scripts/test_e2e.sh`](ops/scripts/test_e2e.sh) to measure.

---

## 9. 트러블슈팅 / Troubleshooting

| 증상 / Symptom                          | 원인 / 해결 / Likely cause / fix                                          |
|----------------------------------------|---------------------------------------------------------------------------|
| `camera.connected: false`              | 잘못된 장치 경로; 컨테이너에 `/dev/video0` 없음; RTSP 도달 불가. **Stats → CAMERA SOURCE** picker (§3.5)로 사용 가능한 카메라 스캔 후 재시작 없이 전환. / Wrong device path; container missing `/dev/video0`; RTSP unreachable. Use the picker (§3.5) to scan available cameras and switch without restart |
| `/api/camera/devices` returns empty    | macOS/Windows + Docker Desktop (호스트 카메라 패스스루 불가) — 네이티브 실행 (§4.4); Linux는 compose의 `devices:` 주석 해제; 네이티브 macOS는 Privacy & Security → Camera 확인. / Run native (§4.4); on Linux uncomment `devices:` in compose; on native macOS check Privacy & Security → Camera |
| `Could not read from resource` / `Parse error` in core logs | RTSP/HTTP URL이 OSCAR 호스트 자신을 가리킴 (`curl -I <URL>`이 OSCAR의 `Access-Control-Allow-Origin: *` 반환). **폰**의 IP 또는 `host.docker.internal` (`make rtsp-server` pull) 사용. §3.3.5 참조. / Points at host itself. Use the **phone's** IP (or `host.docker.internal`). See §3.3.5. |
| iPhone won't show up in picker          | Continuity Camera는 **macOS 13+**, 동일 Apple ID, `make up-local` 필요 (Docker 불가). 또는 Larix → MediaMTX 경로 (§3.3.2). / Continuity Camera needs macOS 13+, same Apple ID, and `make up-local`. Otherwise use Larix → MediaMTX (§3.3.2). |
| `inference.count` stays at 0           | 카메라가 프레임을 생산하지 않음 → Layer 1 확인. / Camera not producing frames → check Layer 1 |
| `inference.count` 증가하나 박스가 안 보임 / climbs but no boxes appear | 모델 미로드. `OSCAR_INFERENCE_STUB=0` 설정 + `models/yolo.onnx` 존재 확인 (§3.4). / Model not loaded. Set `OSCAR_INFERENCE_STUB=0` + ensure `models/yolo.onnx` exists (§3.4). |
| All `abs_pos` are zero                 | Pose POST되지 않음; `/api/stats.pose_known` 확인. / No pose POSTed; check `/api/stats.pose_known` |
| `storage.rows` flat                    | DB 경로 읽기 전용 또는 Localizer 미실행. / DB path read-only or Localizer not running |
| JPEG endpoint 503 once at boot         | 캐시에 프레임 아직 없음 — 부팅 첫 ~1초에는 정상. / No frame in cache yet — normal during the first ~1 second |
| Web UI shows broken image              | `/api/frame.jpg` CORS 프록시에 의해 차단됨 — `oscar-web` (nginx) 경로 사용. / `/api/frame.jpg` blocked by CORS proxy — use `oscar-web` (nginx) path |
| Build fails fetching httplib/json      | 첫 빌드는 네트워크 필요; 프록시 뒤에서는 `HTTPS_PROXY` / `HTTP_PROXY` 설정. / First build needs network; behind proxy set `HTTPS_PROXY` / `HTTP_PROXY` |
| `OSCAR_USE_ONNX=ON` but stub still ran | `ONNXRUNTIME_DIR` env 미설정, 또는 `models/yolo.onnx` 없음. / `ONNXRUNTIME_DIR` env not set, or `models/yolo.onnx` missing |
| `make up-local`: `wait: -n: invalid option` | macOS bash 3.2. 스크립트는 polling으로 대체됨 — 최신 [ops/scripts/up_local.sh](ops/scripts/up_local.sh) 사용. / macOS bash 3.2. Use updated script. |

---

## 10. Git 사용 / Git usage

**[한국어]** 루트에 [.gitignore](.gitignore)가 있어 빌드 산출물(`build/`), Node 모듈, 런타임 데이터(`data/`, `*.sqlite`), 모델(`models/*.onnx`, `*.pt`), secret(`ops/.env`), OS/IDE 잡파일을 자동 제외합니다. 첫 푸시:

**[English]** A [.gitignore](.gitignore) at the root excludes build artefacts (`build/`), Node modules, runtime data (`data/`, `*.sqlite`), models (`models/*.onnx`, `*.pt`), secrets (`ops/.env`), and OS/IDE junk. To publish:

```bash
git init
git add .
git commit -m "OSCAR initial commit"
git branch -M main
git remote add origin git@github.com:<your-org>/<your-repo>.git
git push -u origin main
```

**[한국어]** `ops/.env`는 의도적으로 제외됩니다 — 새 클론은 `make env-init`로 [ops/.env.example](ops/.env.example)에서 생성. 모델 ONNX 파일도 git에 들어가지 않습니다 (`make download-model`로 재생성). 추적된 파일을 사후에 제외하려면:

**[English]** `ops/.env` is intentionally excluded — fresh clones use `make env-init` to seed it from [ops/.env.example](ops/.env.example). Model ONNX files are likewise excluded (re-fetch with `make download-model`). If a file slipped in before the ignore rule, untrack it without deleting:

```bash
git rm -r --cached <path>
git commit -m "stop tracking <path>"
```

---

## 11. 다음 단계 (Post-MVP) / Next steps (post-MVP)

**[한국어]**
- 하드웨어 인코더 (NVENC / VideoToolbox / MediaCodec)를 `Storage::video_writer_loop`에 연결
- IoU-only tracker를 실제 ByteTrack/BoT-SORT (Kalman filter)로 교체
- `/api/frame.jpg` 외에 sub-100ms 브라우저 뷰포트용 WebRTC 추가
- 카메라별 intrinsics (`fx, fy, cx, cy`) 캘리브레이션 및 EXIF / calib tool에서 자동 추출
- `PoseStore`에 IMU 융합 추가 — GNSS 샘플 사이의 sub-second pose 갱신

**[English]**
- Plug a hardware encoder (NVENC / VideoToolbox / MediaCodec) into `Storage::video_writer_loop`
- Replace the IoU-only tracker with a real ByteTrack/BoT-SORT port (Kalman filter)
- Add WebRTC alongside `/api/frame.jpg` for sub-100 ms browser viewport
- Calibrate intrinsics per camera (`fx, fy, cx, cy`) and pull them from EXIF / a calib tool
- IMU fusion in `PoseStore` for sub-second pose updates between GNSS samples

---

_Project name confirmed as **OSCAR**._
