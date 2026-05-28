#include "CameraSource.hpp"

#ifdef OSCAR_HAVE_OPENCV
#include <opencv2/opencv.hpp>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

#include <nlohmann/json.hpp>

namespace oscar::camera {

#ifdef __APPLE__
// Query macOS system_profiler for camera names. Returns names in the order reported.
static std::vector<std::string> macos_camera_names() {
    std::vector<std::string> names;
    FILE* fp = ::popen("system_profiler SPCameraDataType -json 2>/dev/null", "r");
    if (!fp) return names;
    std::string out;
    std::array<char, 4096> buf{};
    while (size_t n = std::fread(buf.data(), 1, buf.size(), fp)) out.append(buf.data(), n);
    ::pclose(fp);
    if (out.empty()) return names;
    try {
        auto j = nlohmann::json::parse(out);
        if (j.contains("SPCameraDataType") && j["SPCameraDataType"].is_array()) {
            for (auto& d : j["SPCameraDataType"]) {
                if (d.contains("_name")) names.push_back(d["_name"].get<std::string>());
            }
        }
    } catch (...) { /* ignore */ }
    return names;
}
#endif


CameraSource::CameraSource(CameraCfg cfg, Pipeline pipe)
    : cfg_(std::move(cfg)), pipe_(std::move(pipe)) {}

CameraSource::~CameraSource() { stop(); }

void CameraSource::start() {
    if (running_.exchange(true)) return;
    th_ = std::thread([this]{ run(); });
}

void CameraSource::stop() {
    if (!running_.exchange(false)) return;
    if (th_.joinable()) th_.join();
}

void CameraSource::set_source(const std::string& src) {
    {
        std::lock_guard<std::mutex> g(cfg_mu_);
        if (cfg_.source == src) return;
        cfg_.source = src;
    }
    need_reopen_ = true;
    LOG_INFO("camera", "source switched to ", src);
}

std::string CameraSource::current_source() const {
    std::lock_guard<std::mutex> g(cfg_mu_);
    return cfg_.source;
}

std::vector<CameraDevice> CameraSource::enumerate() {
    std::vector<CameraDevice> out;
    namespace fs = std::filesystem;

#ifdef __APPLE__
    // macOS: get human-readable names from system_profiler, then emit indexed entries.
    // OpenCV on macOS uses AVFoundation; index order generally matches system_profiler.
    auto names = macos_camera_names();
    for (size_t i = 0; i < names.size(); ++i) {
        CameraDevice cd;
        cd.id = std::to_string(i);
        cd.label = names[i] + " (#" + std::to_string(i) + ")";
        cd.kind = "index";
        cd.available = true;  // best-effort; final answer comes from open() in run()
        out.push_back(cd);
    }
    if (!out.empty()) return out;
#endif

    // Linux /dev/video* — works inside containers when host devices are exposed.
    std::error_code ec;
    if (fs::exists("/dev", ec)) {
        std::vector<std::string> devs;
        for (auto& e : fs::directory_iterator("/dev", ec)) {
            auto name = e.path().filename().string();
            if (name.rfind("video", 0) == 0 && name.size() > 5 &&
                std::all_of(name.begin() + 5, name.end(), ::isdigit)) {
                devs.push_back(e.path().string());
            }
        }
        std::sort(devs.begin(), devs.end());
        for (auto& d : devs) {
            CameraDevice cd;
            cd.id = d;
            cd.label = d;
            cd.kind = "device";
            cd.available = true;
            out.push_back(cd);
        }
    }

    // Last resort: probe a small range of OpenCV indices.
#ifdef OSCAR_HAVE_OPENCV
    if (out.empty()) {
        const int kMaxIndex = 5;
        for (int i = 0; i <= kMaxIndex; ++i) {
            cv::VideoCapture cap;
            bool ok = false;
            try { ok = cap.open(i); } catch (...) { ok = false; }
            if (ok && cap.isOpened()) {
                CameraDevice cd;
                cd.id = std::to_string(i);
                cd.label = "Camera #" + std::to_string(i);
                cd.kind = "index";
                cd.available = true;
                out.push_back(cd);
            }
            cap.release();
        }
    }
#endif

    return out;
}

bool CameraSource::try_open() {
#ifdef OSCAR_HAVE_OPENCV
    return true; // actual open in run()
#else
    return false;
#endif
}

void CameraSource::run() {
#ifdef OSCAR_HAVE_OPENCV
    cv::VideoCapture cap;
    auto open_cap = [&](){
        cap.release();
        std::string src;
        int w, h, fps;
        {
            std::lock_guard<std::mutex> g(cfg_mu_);
            src = cfg_.source;
            w   = cfg_.width;
            h   = cfg_.height;
            fps = cfg_.target_fps;
        }
        try {
            if (src.size() == 1 && std::isdigit(src[0])) {
                cap.open(std::stoi(src));
            } else {
                cap.open(src);
            }
        } catch (...) {}
        if (cap.isOpened()) {
            cap.set(cv::CAP_PROP_FRAME_WIDTH, w);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, h);
            cap.set(cv::CAP_PROP_FPS, fps);
        }
        connected_ = cap.isOpened();
        need_reopen_ = false;
        return cap.isOpened();
    };

    open_cap();

    auto last = std::chrono::steady_clock::now();
    int frames_window = 0;

    while (running_.load()) {
        if (need_reopen_.load()) {
            LOG_INFO("camera", "reopen requested");
            open_cap();
            continue;
        }
        if (!cap.isOpened()) {
            int reconnect_ms;
            std::string src;
            {
                std::lock_guard<std::mutex> g(cfg_mu_);
                reconnect_ms = cfg_.reconnect_ms;
                src = cfg_.source;
            }
            LOG_WARN("camera", "not opened, retry in ", reconnect_ms, "ms src=", src);
            std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_ms));
            open_cap();
            continue;
        }
        cv::Mat m;
        if (!cap.read(m) || m.empty()) {
            connected_ = false;
            int reconnect_ms;
            { std::lock_guard<std::mutex> g(cfg_mu_); reconnect_ms = cfg_.reconnect_ms; }
            LOG_WARN("camera", "read failed, will reconnect");
            std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_ms));
            open_cap();
            continue;
        }
        Frame f;
        f.ts_ms = now_ms();
        f.width = m.cols;
        f.height = m.rows;
        f.channels = m.channels();
        f.seq = ++seq_;
        f.data.assign(m.data, m.data + m.total() * m.elemSize());
        pipe_.frames->push(f);
        pipe_.frames_rec->push(f);
        pipe_.frames_ws->push(f);

        ++frames_window;
        auto nowt = std::chrono::steady_clock::now();
        auto el = std::chrono::duration_cast<std::chrono::milliseconds>(nowt - last).count();
        if (el >= 1000) {
            fps_ = frames_window * 1000.f / el;
            frames_window = 0;
            last = nowt;
        }
    }
#else
    LOG_WARN("camera", "OpenCV unavailable — emitting synthetic frames");
    synthetic_frame_loop();
#endif
}

void CameraSource::synthetic_frame_loop() {
    const int w = cfg_.width, h = cfg_.height;
    auto last = std::chrono::steady_clock::now();
    int win = 0;
    int t = 0;
    while (running_.load()) {
        Frame f;
        f.ts_ms = now_ms();
        f.width = w;
        f.height = h;
        f.channels = 3;
        f.seq = ++seq_;
        f.data.assign(size_t(w) * h * 3, 0);
        // synthetic moving square (so detector stub can find motion)
        int cx = (t * 4) % w;
        int cy = h / 2;
        int sz = 60;
        for (int y = cy - sz/2; y < cy + sz/2 && y < h; ++y) {
            if (y < 0) continue;
            for (int x = cx - sz/2; x < cx + sz/2 && x < w; ++x) {
                if (x < 0) continue;
                size_t idx = (size_t(y) * w + x) * 3;
                f.data[idx + 0] = 50;
                f.data[idx + 1] = 200;
                f.data[idx + 2] = 50;
            }
        }
        ++t;
        connected_ = true;
        pipe_.frames->push(f);
        pipe_.frames_rec->push(f);
        pipe_.frames_ws->push(f);

        ++win;
        auto nowt = std::chrono::steady_clock::now();
        auto el = std::chrono::duration_cast<std::chrono::milliseconds>(nowt - last).count();
        if (el >= 1000) { fps_ = win * 1000.f / el; win = 0; last = nowt; }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / std::max(1, cfg_.target_fps)));
    }
}

} // namespace oscar::camera
