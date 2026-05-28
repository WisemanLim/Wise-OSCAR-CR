# Product Requirement Document (PRD)

## 프로젝트명: OSCAR (Open-source Spatial Camera tracking and Autonomous Recognition)

본 문서는 실시간으로 카메라의 연결을 진단하고, 움직이는 카메라 환경에서 객체 탐지(Object Detection) 및 추적(Tracking)을 수행하며, 카메라의 물리적 위치 변화를 반영한 상대/절대 좌표 공간 매핑을 지원하는 초저지연 크로스플랫폼 시스템 **OSCAR**에 대한 제품 요구사항 정의서(PRD)입니다.

---

## 1. 프로젝트명 후보 및 약어 제안

사용자가 선택할 수 있는 프로젝트명 후보와 그 약어(Abbreviation)는 다음과 같습니다. 선택된 프로젝트명에 따라 본 파일명은 `[약어]_PRD.md`로 변경됩니다.

| 후보 번호    | 프로젝트 이름                                                                     | 약어        | 의미 및 특징                                                                                                     |
| ------------ | --------------------------------------------------------------------------------- | ----------- | ---------------------------------------------------------------------------------------------------------------- |
| **1 (추천)** | **ROAM** (Real-time Object tracking with Absolute & Relative Motion)              | **ROAM**    | 움직이는 카메라 환경에서 절대/상대 운동을 실시간으로 추적 및 공간을 '배회(Roam)'하며 모니터링함을 의미           |
| **2**        | **Kestrel** (Kinetic Edge-system for Spatial Tracking and Real-time Localization) | **KESTREL** | 기동성이 뛰어난 황조롱이(Kestrel)처럼 이동 카메라 환경에서도 객체와 공간 위치를 흔들림 없이 실시간 추적함을 상징 |
| **3**        | **OSCAR** (Open-source Spatial Camera tracking and Autonomous Recognition)        | **OSCAR**   | 오픈소스 기반의 공간 카메라 추적 및 자율 객체 인식 플랫폼                                                        |
| **4**        | **VORTEX** (Visual Object Real-time Tracking & Edge-localization System)          | **VORTEX**  | 대량의 실시간 비디오 스트림 내에서 객체를 신속하게 탐지하고 엣지단에서 위치를 실시간 매핑하는 허브               |

---

## 2. 요구사항 상세 분석 및 해결 방안

### 요건 1: 카메라 연결 확인 (내/외부용)

- **요구사항**: 시스템에서 사용 가능한 내장 웹캠 및 외장 USB 카메라, IP 카메라인 RTSP 스트림 연결을 감지 및 모니터링해야 합니다.
- **해결 방안**:
  - **OpenCV (`VideoCapture`)**: 디바이스 인덱스(`0`, `1`, ...) 및 RTSP URL 스트림 주소를 스캔하여 연결 가능 여부를 판별합니다.
  - **OS 레벨 API**: Windows (Media Foundation), macOS (AVFoundation), Android (Camera2 API), iOS (AVFoundation)와 네이티브 연결을 제공하는 크로스플랫폼 카메라 API를 활용합니다.
  - **상태 체크**: 백그라운드 핑(Ping) 또는 프레임 수신율(FPS) 저하 감지 시 재연결(Reconnection) 로직을 실행합니다.

### 요건 2: Object Detection (obj-dtc)

- **요구사항**: 카메라 영상을 받아 실시간으로 움직이는 물체(객체)를 감지합니다.
- **해결 방안**:
  - **YOLOv8 / YOLO11 (Ultralytics)**: SOTA급의 연산 속도와 정확도를 제공하는 기본 감지기로 채택합니다.
  - **YOLO-World (Open-Vocabulary)**: 고정된 클래스 외에 임의의 텍스트 쿼리(예: "레드 헬멧을 쓴 사람", "검정 가방")로 실시간 탐지가 필요할 경우 확장 레이어로 검토합니다.
  - **경량화**: 기기 사양에 맞춰 FP16/INT8 양자화된 ONNX 및 TensorRT, CoreML, TFLite 모델을 활용합니다.

### 요건 3: Multi-Object Tracking (ID별 추적 관리)

- **요구사항**: 다중 객체가 존재하더라도 각 객체마다 고유 ID를 발급하고, 궤적(Tracking History)을 개별 저장·관리합니다.
- **해결 방안**:
  - **ByteTrack / BoT-SORT**: 칼만 필터(Kalman Filter)와 헝가리안 알고리즘(Hungarian Algorithm) 기반의 바운딩 박스 오버랩(IoU) 및 모션 예측을 적용하여 차폐(Occlusion)가 발생해도 ID를 유지합니다.
  - **추적 데이터 구조**: `{ object_id: Int, class: String, bbox: [x, y, w, h], velocity: Float, trajectory: List[Point] }` 형태로 로컬 메모리에 캐싱한 후 고속 DB에 기록합니다.

### 요건 4: Dynamic Camera Localization (카메라 이동 대응 및 좌표계 매핑)

- **요구사항**: 카메라가 이동하더라도 시스템의 현재 위치를 기록하고, 객체의 추적을 절대좌표와 카메라 기준 상대좌표 모두 계산하여 저장해야 합니다.
- **해결 방안**:
  - **시스템(카메라) 위치 측정**:
    - 외부 GPS 센서, 모바일/태블릿 내장 GNSS 센서, 혹은 실내 위치 측위 시스템(UWB, IMU 센서 데이터 통합)으로부터 위치 좌표 $(X_{cam}, Y_{cam}, Z_{cam})$ 및 카메라 요(Yaw), 피치(Pitch), 롤(Roll) 등의 자세각을 수집합니다.
  - **좌표 변환 프레임워크 (상대 $\leftrightarrow$ 절대)**:
    1. **상대 좌표**: 카메라의 뷰포트 평면 상의 픽셀 좌표 $(u, v)$ 또는 카메라 조리개 원점 기준의 3D 상대 위치 $(x_{rel}, y_{rel}, z_{rel})$.
    2. **투영 및 호모그래피 변환**: 카메라 캘리브레이션 파라미터(내부 파라미터 $K$, 외부 파라미터 $[R|t]$)를 이용해 픽셀 공간 좌표를 지면 혹은 3D 월드 공간 좌표로 투영합니다.
       $$\begin{bmatrix} x_{world} \\ y_{world} \\ z_{world} \end{bmatrix} = R^{-1} \left( K^{-1} d \begin{bmatrix} u \\ v \\ 1 \end{bmatrix} - t \right)$$
       (여기서 $d$는 객체까지의 추정 거리 또는 지면 평면 가정 하의 깊이 값)
    3. **절대 좌표**: 카메라 자체의 세계 좌표 $(X_{cam}, Y_{cam})$에 객체의 상대 좌표 벡터를 더해 절대 공간 좌표 $(X_{abs}, Y_{abs})$를 구하고 저장합니다.

### 요건 5: Object 특징 분기 및 상태 관리

- **요구사항**: 사람, 사물, 동물 등의 대분류 특징을 구분하고 정지/이동 상태를 분기 관리합니다.
- **해결 방안**:
  - **카테고리 분류**: YOLO의 Class ID를 기반으로 대분류(사람: `Person`, 동물: `Dog/Cat/Bird...`, 사물/탈것: `Car/Bicycle...`, 정지 물건: `Chair/Luggage...`)로 매핑합니다.
  - **동적 상태 판별**: 객체의 최근 $N$ 프레임 동안의 월드 좌표계 이동 속도 벡터 평균 $||\vec{v}||$를 구하여, 기준 임계값(예: $0.1 m/s$) 이하일 경우 `Stationary` (정지), 초과할 경우 `Moving` (이동) 상태로 판별합니다.

### 요건 6: 실시간 영상 저장 및 초저지연 (< 500ms Latency)

- **요구사항**: 영상 실시간 저장 및 객체 탐지 프레임 저장 시 딜레이가 500ms 이하여야 합니다.
- **해결 방안**:
  - **비동기 파이프라인 (Producer-Consumer)**: 프레임 캡처 스레드, 인공지능 추론 스레드, 영상 인코딩 및 파일 저장 스레드를 완전 독립적으로 구성하여 메인 스트림 병목을 방지합니다.
  - **영상 인코딩**: CPU 성능 부담을 줄이기 위해 하드웨어 가속 인코더(Nvidia NVENC, Apple VideoToolbox, Intel QuickSync, Android MediaCodec)를 활성화하여 실시간 H.264 MP4로 저장합니다.
  - **스트리밍 프로토콜**: 로컬 웹 브라우저 전송 시 지연 시간이 1~2초가 발생하는 HLS/DASH 대신, 100ms 미만의 지연을 보장하는 **WebRTC** 또는 **Low-Latency WebSocket Stream**을 사용하여 브라우저로 뷰포트를 전송합니다.

### 요건 7: 크로스플랫폼 클라이언트 지원 (Windows, macOS, Android, iOS)

- **요구사항**: 하나의 코드 베이스로 다양한 데스크톱 및 모바일 OS 환경을 커버해야 합니다.
- **해결 방안**:
  - **UI 단**: **Tauri v2** 또는 **Flutter**를 사용해 데스크톱(Windows/macOS) 및 모바일(Android/iOS) 애플리케이션을 한번에 빌드합니다.
  - **연산 백엔드 분할**:
    - **모바일/엣지 네이티브 모드**: 모바일 기기 자체 연산 시 **ONNX Runtime Mobile C++ API** 또는 **TensorFlow Lite**를 네이티브 플러그인(Tauri Rust Native / Flutter C-FFI)을 통해 구동하여 하드웨어 가속(CoreML, NNAPI)을 태웁니다.
    - **서버 오프로딩 모드**: 저사양 클라이언트의 경우 로컬 서버로 영상 스트림을 보내고, 서버(Python/C++)가 연산 후 결과 좌표 정보만 WebSocket으로 클라이언트에 돌려주는 구조를 택합니다.

### 요건 8: 데스크탑 앱 형태 및 로컬 서버 웹 접속 지원

- **요구사항**: 설치형 데스크탑 프로그램으로 실행될 뿐만 아니라, 로컬망에 띄워진 웹 서버를 통해 다른 PC나 스마트폰 브라우저에서도 접근 가능해야 합니다.
- **해결 방안**:
  - **Tauri 내장 웹 서버 또는 독자적 로컬 서버**:
    - Rust 기반의 **Axum** 또는 Python 기반의 **FastAPI** 서버를 백그라운드에 구동시킵니다.
    - 클라이언트 앱 실행 시 백그라운드에서 로컬 웹 서버(`http://localhost:8080`)가 함께 실행되어, 브라우저로 접속해도 데스크탑 앱과 동일한 화면 및 관제 기능을 제공합니다.

---

## 3. 오픈소스 조합 비교 및 추천

사용자 스타 히스토리([WisemanLim-stars-20260523-121255.md](file:///Users/wise/Developer/DA%28Python,R,...%29/pub-star-classifier/output_test/WisemanLim-stars-20260523-121255.md)) 및 SOTA 오픈소스를 조합하여 구상할 수 있는 아키텍처 옵션 3가지를 비교합니다.

### 조합 A (강력 추천): Rust & Tauri 중심의 경량 초고성능 구조

- **주요 구성**:
  - **Client UI**: **Tauri v2** (Rust + React/Vite/TS) - Desktop & Mobile 크로스플랫폼 패키징
  - **AI & Processing Engine**: **Rust binding OpenCV** + **ONNX Runtime (Rust SDK)**
  - **Detection & Tracking**: **Ultralytics YOLO (ONNX format)** + **ByteTrack (Rust/C++ Port)**
  - **Local Web Server**: **Axum** (Tauri 앱 백그라운드에서 실행)
  - **Database**: **SQLite** (Rust `rusqlite` 또는 `sqlx`로 초고속 로컬 저장)
- **비교 분석**:
  - **장점**: Rust의 메모리 안정성과 Zero-cost Abstraction 덕분에 프레임 드랍이 거의 없으며, 단일 바이너리로 컴파일되어 패키지 용량이 매우 작습니다(수십 MB 수준). Tauri v2가 모바일까지 완벽히 지원하므로 네이티브 연동이 매끄럽습니다.
  - **단점**: Rust 기반의 OpenCV 및 ONNX 연동 코딩 난이도가 다소 높고 학습 곡선이 있습니다.

### 조합 B: Flutter & Python FastAPI 실용적 생산성 구조

- **주요 구성**:
  - **Client UI**: **Flutter** (Dart) - 모바일 및 데스크톱 UI 제작
  - **Local Server Backend**: **Python FastAPI**
  - **Detection & Tracking**: **Ultralytics YOLOv8/11 (Python PyTorch/TensorRT)** + **Roboflow Trackers** (ByteTrack/BoT-SORT Python)
  - **Database**: **PocketBase** 또는 **SQLite**
- **비교 분석**:
  - **장점**: Python 생태계의 풍부한 AI 라이브러리(`ultralytics`, `opencv-python`, `trackers`)를 100% 활용할 수 있어 개발 속도가 가장 빠릅니다. Flutter는 UI 컴포넌트가 풍부하고 크로스플랫폼 완성도가 매우 높습니다.
  - **단점**: 사용자가 데스크톱 앱을 실행할 때 백그라운드에 Python 런타임(가상환경 또는 내장 파이썬 배포판)을 함께 실행시켜야 하므로 배포 패키지 용량(300MB+)과 메모리 점유율이 큽니다.

### 조합 C: C++ Core Engine & React Web GUI 극단적 지연 차단 구조

- **주요 구성**:
  - **Client UI**: **React / WebRTC** (브라우저 및 Electron 래퍼)
  - **Local Server Backend**: **C++20 Native Server** (Crow C++ Web Framework)
  - **Detection & Tracking**: **C++ OpenCV** + **ONNX Runtime (C++ API)** + **ByteTrack (Original C++ implementation)**
  - **Database**: **SQLite3 C++ API**
- **비교 분석**:
  - **장점**: 지연율(Latency) 면에서 가장 극단적인 성능 최적화가 가능합니다. GPU 가속(CUDA/TensorRT, Apple Silicon MPS)과의 최하단 다이렉트 연동으로 딜레이가 50ms 미만으로 수렴합니다.
  - **단점**: 크로스플랫폼 빌드 시스템(CMake, Android NDK, iOS CocoaPods 등)을 각 플랫폼마다 C++ 라이브러리를 동적/정적 링크하여 구성해야 하므로 유지 보수 및 개발 난이도가 매우 높습니다.

### 3가지 조합 비교 요약표

| 항목                       | 조합 A (Tauri + Rust/ONNX)     | 조합 B (Flutter + Python/FastAPI) | 조합 C (C++ Core + Web GUI)   |
| -------------------------- | ------------------------------ | --------------------------------- | ----------------------------- |
| **개발 생산성**            | 보통                           | **매우 높음**                     | 낮음                          |
| **메모리/CPU 효율**        | **우수** (Rust)                | 보통 (Python 오버헤드 있음)       | **최상** (C++ 네이티브)       |
| **클라이언트 배포 용량**   | **최소 (50MB 내외)**           | 큼 (300MB 이상, 파이썬 포함 시)   | 보통 (50~100MB)               |
| **실시간 딜레이 (<500ms)** | 매우 안정적 수렴               | 적절한 가속 시 가능               | **최상의 지연 차단 (최소)**   |
| **모바일 포팅 난이도**     | 보통 (Rust 크로스 컴파일 필요) | **쉬움** (Flutter 플러그인 이용)  | 매우 어려움 (NDK/Native 연동) |

---

## 4. 아키텍처 및 데이터 흐름 설계 (조합 A 기준)

```mermaid
graph TD
    subgraph Client Platform (Tauri v2 UI / Browser)
        A[WebRTC / WebSocket Viewport] -->|User Control & Config| B(Tauri Core / Axum API Server)
        GNSS[GNSS/IMU Sensor API] -->|Cam Position & Attitude| B
    end

    subgraph Dynamic Edge Server / Local Service (Rust)
        B -->|Initialize stream| C[OpenCV VideoCapture]
        C -->|Frames| D[Async Frame Queue]
        D -->|Pop Frame| E[ONNX Runtime / TensorRT Inference]
        E -->|BBoxes & Scores| F[ByteTrack Kalman Tracker]

        %% Coordinate Processing
        F -->|Tracked Objects ID, BBox| G[Localization Engine]
        GNSS -->|Camera Lat/Lng, Yaw/Pitch| G
        G -->|Compute Rel & Abs Coordinates| H[Coordinate Transform Homography]

        %% Recording and Storage
        D -->|Raw Frame Stream| I[FFmpeg H.264 Hardward Enc]
        I -->|Video Files| Storage[(Local SSD Storage - MP4)]

        H -->|Tracking Log & Absolute Positions| DB[(SQLite DB)]
        B -->|Fetch Live Frame & Meta| A
    end
```

---

## 5. 추천 결론 및 마일스톤

본 시스템의 요구 조건(실시간성, 크로스플랫폼, 초저지연, 설치형 앱 + 브라우저 접속)을 충족하기 위한 최선의 선택은 **조합 A (Tauri v2 + Rust Backend + React/TS Frontend)** 입니다.

1. **성능 요건 수렴**: Rust로 동작하는 OpenCV 프레임 캡처링 및 ONNX Runtime 추론은 Python에 비해 불필요한 GC(Garbage Collection)나 GIL(Global Interpreter Lock)이 없어 500ms 미만의 지연 성능(실제 100ms 내외)을 매우 안정적으로 달성합니다.
2. **배포 편리성**: 클라이언트는 별도의 Python 설치 없이 실행 파일 하나로 설치가 완료되며, 백그라운드에서 구동되는 로컬 웹 서버를 브라우저로도 동시 제공하여 모니터링 허브 역할을 완벽하게 수행합니다.

---

_본 문서는 기획 사양 확정 이후 세부 소스코드 아키텍처 및 데이터베이스 스키마 설계로 확장됩니다._
_본 프로젝트의 공식 명칭은 **OSCAR**로 확정되었습니다._
