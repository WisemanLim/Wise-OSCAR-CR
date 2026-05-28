#pragma once
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <nlohmann/json.hpp>

namespace oscar {

struct CameraCfg {
    std::string source = "0";   // "0" | "/dev/video0" | "rtsp://..."
    int target_fps = 30;
    int width = 1280;
    int height = 720;
    int reconnect_ms = 2000;
};

struct InferenceCfg {
    std::string model_path = "models/yolo.onnx";
    std::string labels_path = "models/coco.names";
    float conf_threshold = 0.25f;
    float iou_threshold = 0.45f;
    int input_size = 640;
    std::string provider = "cpu";   // "cpu" | "cuda" | "coreml"
    bool stub_mode = true;          // when model file missing, generate synthetic dets
};

struct TrackerCfg {
    float match_thresh = 0.8f;
    int track_buffer = 30;
    float det_thresh = 0.5f;
    int frame_rate = 30;
};

struct LocalizationCfg {
    // intrinsic K (fx, fy, cx, cy)
    float fx = 800.f, fy = 800.f, cx = 640.f, cy = 360.f;
    float ground_height_m = 0.0f;    // ground plane assumption
    float camera_height_m = 1.5f;    // for ground-projection
    float static_velocity_thr = 0.1f;
};

struct StorageCfg {
    std::string db_path = "data/oscar.sqlite";
    std::string video_dir = "data/videos";
    int segment_seconds = 60;
    bool enable_video = true;
};

struct ServerCfg {
    std::string bind = "0.0.0.0";
    int port = 8080;
    int ws_port = 8081;
    std::string web_root = "web/dist";
};

struct AppConfig {
    CameraCfg camera;
    InferenceCfg inference;
    TrackerCfg tracker;
    LocalizationCfg localization;
    StorageCfg storage;
    ServerCfg server;
};

inline AppConfig load_config(const std::string& path) {
    AppConfig c;
    std::ifstream f(path);
    if (!f) return c;
    nlohmann::json j;
    try { f >> j; } catch (...) { return c; }

    auto get = [&](auto& dst, const char* k, const auto& def) {
        try { dst = j.value(k, def); } catch (...) {}
    };

    if (j.contains("camera")) {
        auto& cc = j["camera"];
        c.camera.source       = cc.value("source", c.camera.source);
        c.camera.target_fps   = cc.value("target_fps", c.camera.target_fps);
        c.camera.width        = cc.value("width", c.camera.width);
        c.camera.height       = cc.value("height", c.camera.height);
        c.camera.reconnect_ms = cc.value("reconnect_ms", c.camera.reconnect_ms);
    }
    if (j.contains("inference")) {
        auto& ic = j["inference"];
        c.inference.model_path     = ic.value("model_path", c.inference.model_path);
        c.inference.labels_path    = ic.value("labels_path", c.inference.labels_path);
        c.inference.conf_threshold = ic.value("conf_threshold", c.inference.conf_threshold);
        c.inference.iou_threshold  = ic.value("iou_threshold", c.inference.iou_threshold);
        c.inference.input_size     = ic.value("input_size", c.inference.input_size);
        c.inference.provider       = ic.value("provider", c.inference.provider);
        c.inference.stub_mode      = ic.value("stub_mode", c.inference.stub_mode);
    }
    if (j.contains("tracker")) {
        auto& tc = j["tracker"];
        c.tracker.match_thresh = tc.value("match_thresh", c.tracker.match_thresh);
        c.tracker.track_buffer = tc.value("track_buffer", c.tracker.track_buffer);
        c.tracker.det_thresh   = tc.value("det_thresh", c.tracker.det_thresh);
        c.tracker.frame_rate   = tc.value("frame_rate", c.tracker.frame_rate);
    }
    if (j.contains("localization")) {
        auto& lc = j["localization"];
        c.localization.fx = lc.value("fx", c.localization.fx);
        c.localization.fy = lc.value("fy", c.localization.fy);
        c.localization.cx = lc.value("cx", c.localization.cx);
        c.localization.cy = lc.value("cy", c.localization.cy);
        c.localization.ground_height_m   = lc.value("ground_height_m", c.localization.ground_height_m);
        c.localization.camera_height_m   = lc.value("camera_height_m", c.localization.camera_height_m);
        c.localization.static_velocity_thr = lc.value("static_velocity_thr", c.localization.static_velocity_thr);
    }
    if (j.contains("storage")) {
        auto& sc = j["storage"];
        c.storage.db_path         = sc.value("db_path", c.storage.db_path);
        c.storage.video_dir       = sc.value("video_dir", c.storage.video_dir);
        c.storage.segment_seconds = sc.value("segment_seconds", c.storage.segment_seconds);
        c.storage.enable_video    = sc.value("enable_video", c.storage.enable_video);
    }
    if (j.contains("server")) {
        auto& sv = j["server"];
        c.server.bind     = sv.value("bind", c.server.bind);
        c.server.port     = sv.value("port", c.server.port);
        c.server.ws_port  = sv.value("ws_port", c.server.ws_port);
        c.server.web_root = sv.value("web_root", c.server.web_root);
    }

    // env overrides — useful for docker compose .env and ad-hoc shell runs
    auto env_str = [](const char* k) -> const char* {
        const char* v = std::getenv(k);
        return (v && *v) ? v : nullptr;
    };
    auto env_int = [&](const char* k, int dflt) {
        if (const char* v = env_str(k)) { try { return std::stoi(v); } catch (...) {} }
        return dflt;
    };
    if (auto* v = env_str("OSCAR_CAMERA_SOURCE")) c.camera.source = v;
    if (auto* v = env_str("OSCAR_INFERENCE_MODEL")) c.inference.model_path = v;
    c.inference.stub_mode = env_str("OSCAR_INFERENCE_STUB")
        ? (std::string(env_str("OSCAR_INFERENCE_STUB")) != "0" &&
           std::string(env_str("OSCAR_INFERENCE_STUB")) != "false")
        : c.inference.stub_mode;
    c.server.port = env_int("OSCAR_HTTP_PORT", c.server.port);
    if (auto* v = env_str("OSCAR_HTTP_BIND")) c.server.bind = v;

    return c;
}

} // namespace oscar
