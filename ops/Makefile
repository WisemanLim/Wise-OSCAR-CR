# OSCAR ops Makefile
# Run from repo root via: make -C ops <target>
# Or: cd ops && make <target>

ROOT       := $(abspath $(CURDIR)/..)
ENV_FILE   := $(ROOT)/ops/.env
COMPOSE_BASE := docker compose -f "$(ROOT)/ops/docker-compose.yml"
# Auto-load ops/.env (overrides OSCAR_* vars) when present.
ifneq ("$(wildcard $(ENV_FILE))","")
  COMPOSE := $(COMPOSE_BASE) --env-file "$(ENV_FILE)"
else
  COMPOSE := $(COMPOSE_BASE)
endif
CORE_DIR   := $(ROOT)/core
BUILD_DIR  := $(ROOT)/build
WEB_DIR    := $(ROOT)/web
DATA_DIR   := $(ROOT)/data

.PHONY: help build up up-local up-rtsp env-init down restart logs ps clean
.PHONY: rtsp-server rtsp-server-stop rtsp-server-logs
.PHONY: download-model
.PHONY: build-core build-web run-core run-web dev-core dev-web
.PHONY: test test-curl test-layer1 test-layer2 test-layer3 test-layer4 test-layer5 test-layer6 test-e2e
.PHONY: image-core image-web

help:
	@echo "OSCAR ops targets:"
	@echo "  --- docker compose ---"
	@echo "  build           Build both images (core + web)"
	@echo "  up              Start oscar-core + oscar-web (detached, auto-loads ops/.env)"
	@echo "  up-rtsp         Same as 'up' but requires ops/.env with OSCAR_CAMERA_SOURCE set"
	@echo "  down            Stop and remove containers"
	@echo "  restart         down + up"
	@echo "  logs            Tail logs"
	@echo "  ps              Show container status"
	@echo "  env-init        Copy ops/.env.example -> ops/.env (won't overwrite)"
	@echo "  --- rtsp relay (for iOS / push-only sources) ---"
	@echo "  rtsp-server     Run mediamtx (rtsp:8554, rtmp:1935, hls:8888)"
	@echo "  rtsp-server-stop  Stop relay"
	@echo "  rtsp-server-logs  Tail relay logs"
	@echo "  --- model ---"
	@echo "  download-model  Export yolov8n.onnx into models/ (via ultralytics docker)"
	@echo "  --- native (no docker) ---"
	@echo "  up-local        One-shot: build core + run core + vite dev (Ctrl+C stops both)"
	@echo "  build-core      cmake build core binary"
	@echo "  build-web       vite production build"
	@echo "  run-core        Run built binary"
	@echo "  run-web         Serve built web (vite preview)"
	@echo "  dev-core        Run core in dev (host build)"
	@echo "  dev-web         Run vite dev server (HMR)"
	@echo "  --- tests ---"
	@echo "  test            Run all layer tests (assumes services up)"
	@echo "  test-layerN     Run layer N test script (1..6)"
	@echo "  test-e2e        End-to-end smoke test"
	@echo "  --- misc ---"
	@echo "  clean           rm build/ and data/"

build:
	$(COMPOSE) build

up:
	$(COMPOSE) up -d
	@echo "oscar-core: http://localhost:8080  oscar-web: http://localhost:5173"
	@test -f "$(ENV_FILE)" && echo "(using $(ENV_FILE))" || true

up-rtsp:
	@test -f "$(ENV_FILE)" || { \
	  echo "ERR: $(ENV_FILE) not found. Run 'make env-init' then edit it (set OSCAR_CAMERA_SOURCE=rtsp://...)."; \
	  exit 1; \
	}
	@grep -E '^OSCAR_CAMERA_SOURCE=.+' "$(ENV_FILE)" >/dev/null || { \
	  echo "ERR: OSCAR_CAMERA_SOURCE is empty in $(ENV_FILE). Set it to an rtsp:// (or other) URL."; \
	  exit 1; \
	}
	$(COMPOSE) up -d
	@echo "RTSP source from $(ENV_FILE) applied."
	@echo "oscar-core: http://localhost:$${OSCAR_HTTP_PORT:-8080}  oscar-web: http://localhost:5173"

env-init:
	@test -f "$(ENV_FILE)" && { echo "$(ENV_FILE) already exists — not overwriting."; exit 0; } || true
	cp "$(ROOT)/ops/.env.example" "$(ENV_FILE)"
	@echo "created $(ENV_FILE) — edit OSCAR_CAMERA_SOURCE before 'make up-rtsp'"

up-local:
	"$(ROOT)/ops/scripts/up_local.sh"

# ---------- RTSP relay (for Larix Broadcaster on iOS / push-only sources) ----------
# Phone pushes to mediamtx (8554/rtsp, 1935/rtmp), OSCAR pulls from mediamtx.
RTSP_RELAY_CONTAINER := oscar-rtsp-relay
rtsp-server:
	@docker inspect $(RTSP_RELAY_CONTAINER) >/dev/null 2>&1 && \
	  { echo "$(RTSP_RELAY_CONTAINER) already running. 'make rtsp-server-stop' first."; exit 0; } || true
	docker run -d --rm --name $(RTSP_RELAY_CONTAINER) \
	  -p 8554:8554 -p 1935:1935 -p 8888:8888 \
	  bluenviron/mediamtx:latest
	@echo "RTSP relay up:"
	@echo "  - phone pushes to:  rtsp://<MAC_IP>:8554/<stream>  (or rtmp://<MAC_IP>:1935/<stream>)"
	@echo "  - OSCAR pulls from: rtsp://host.docker.internal:8554/<stream>  (Docker) or rtsp://<MAC_IP>:8554/<stream> (native)"
	@echo "  - browser preview:  http://localhost:8888/<stream>"

rtsp-server-stop:
	-docker stop $(RTSP_RELAY_CONTAINER) 2>/dev/null
	@echo "rtsp relay stopped"

rtsp-server-logs:
	docker logs -f $(RTSP_RELAY_CONTAINER)

# ---------- Real YOLO model (OpenCV DNN backend) ----------
# Exports YOLOv8n to ONNX via the Ultralytics docker image (~6MB onnx output),
# then drops it at models/yolo.onnx for the core to pick up.
MODEL_PATH := $(ROOT)/models/yolo.onnx
download-model:
	@mkdir -p "$(ROOT)/models"
	@if [ -f "$(MODEL_PATH)" ]; then \
	  echo "$(MODEL_PATH) already exists ($$(du -h "$(MODEL_PATH)" | cut -f1))."; \
	  echo "Delete it first to re-download."; exit 0; \
	fi
	@echo "Exporting YOLOv8n via ultralytics docker (one-time, ~5GB image pull)..."
	docker run --rm -v "$(ROOT)/models:/work" -w /work \
	  ultralytics/ultralytics:latest \
	  yolo export model=yolov8n.pt format=onnx imgsz=640 opset=12
	mv "$(ROOT)/models/yolov8n.onnx" "$(MODEL_PATH)"
	@echo "OK: $(MODEL_PATH)"
	@echo "Next: set OSCAR_INFERENCE_STUB=0 in ops/.env, then 'make restart'."

down:
	$(COMPOSE) down

restart: down up

logs:
	$(COMPOSE) logs -f --tail=200

ps:
	$(COMPOSE) ps

image-core:
	$(COMPOSE) build oscar-core

image-web:
	$(COMPOSE) build oscar-web

# ---------- Local build (no docker) ----------
build-core:
	cmake -S "$(CORE_DIR)" -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=Release
	cmake --build "$(BUILD_DIR)" -j

build-web:
	cd "$(WEB_DIR)" && npm install && npm run build

run-core: build-core
	cd "$(ROOT)" && "$(BUILD_DIR)/oscar_core" --config "$(ROOT)/config/oscar.json"

run-web: build-web
	cd "$(WEB_DIR)" && npm run preview

dev-core: build-core
	cd "$(ROOT)" && "$(BUILD_DIR)/oscar_core" --config "$(ROOT)/config/oscar.json"

dev-web:
	cd "$(WEB_DIR)" && npm install && npm run dev

# ---------- Tests ----------
TEST_BASE ?= http://localhost:8080

test: test-layer1 test-layer2 test-layer3 test-layer4 test-layer5 test-layer6 test-e2e
	@echo "[OK] all layer tests passed"

test-layer1:
	@echo "--- Layer 1 (camera) ---"
	"$(ROOT)/ops/scripts/test_layer1_camera.sh" $(TEST_BASE)

test-layer2:
	@echo "--- Layer 2 (inference) ---"
	"$(ROOT)/ops/scripts/test_layer2_inference.sh" $(TEST_BASE)

test-layer3:
	@echo "--- Layer 3 (tracking) ---"
	"$(ROOT)/ops/scripts/test_layer3_tracking.sh" $(TEST_BASE)

test-layer4:
	@echo "--- Layer 4 (localization) ---"
	"$(ROOT)/ops/scripts/test_layer4_localization.sh" $(TEST_BASE)

test-layer5:
	@echo "--- Layer 5 (storage) ---"
	"$(ROOT)/ops/scripts/test_layer5_storage.sh" $(TEST_BASE)

test-layer6:
	@echo "--- Layer 6 (server) ---"
	"$(ROOT)/ops/scripts/test_layer6_server.sh" $(TEST_BASE)

test-e2e:
	@echo "--- E2E ---"
	"$(ROOT)/ops/scripts/test_e2e.sh" $(TEST_BASE)

test-curl:
	curl -fsS $(TEST_BASE)/api/health
	@echo

clean:
	rm -rf "$(BUILD_DIR)" "$(DATA_DIR)" "$(WEB_DIR)/dist" "$(WEB_DIR)/node_modules"
