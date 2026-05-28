#include "Server.hpp"
#include "../common/Logger.hpp"
#include "../localization/PoseStore.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#ifdef OSCAR_HAVE_OPENCV
#include <opencv2/opencv.hpp>
#endif

#include <chrono>
#include <atomic>
#include <filesystem>
#include <vector>

using json = nlohmann::json;

namespace oscar::server {

static json track_to_json(const TrackedObject& o) {
    return {
        {"id", o.id},
        {"category", o.category},
        {"state", o.state},
        {"class_name", o.bbox.class_name},
        {"score", o.bbox.score},
        {"bbox", {o.bbox.x, o.bbox.y, o.bbox.w, o.bbox.h}},
        {"rel_pos", {o.rel_pos.x, o.rel_pos.y, o.rel_pos.z}},
        {"abs_pos", {o.abs_pos.x, o.abs_pos.y, o.abs_pos.z}},
        {"velocity_px", {o.velocity_px.x, o.velocity_px.y}},
        {"ts_ms", o.ts_ms}
    };
}

static json row_to_json(const storage::TrackRow& r) {
    return {
        {"ts_ms", r.ts_ms},
        {"object_id", r.object_id},
        {"category", r.category},
        {"state", r.state},
        {"class_name", r.class_name},
        {"score", r.score},
        {"bbox", {r.bbox_x, r.bbox_y, r.bbox_w, r.bbox_h}},
        {"rel_pos", {r.rel_x, r.rel_y, r.rel_z}},
        {"abs_pos", {r.abs_x, r.abs_y, r.abs_z}}
    };
}

Server::Server(ServerCfg cfg, Pipeline pipe, storage::Storage* store, AppStats stats)
    : cfg_(std::move(cfg)), pipe_(std::move(pipe)), store_(store), stats_(std::move(stats)) {}

Server::~Server() { stop(); }

void Server::start() {
    if (running_.exchange(true)) return;
    th_frame_ = std::thread([this]{ frame_cache_loop(); });
    th_track_ = std::thread([this]{ track_cache_loop(); });
    th_http_  = std::thread([this]{ http_loop(); });
}

void Server::stop() {
    if (!running_.exchange(false)) return;
    if (server_handle_) {
        auto* svr = static_cast<httplib::Server*>(server_handle_);
        svr->stop();
    }
    if (th_http_.joinable())  th_http_.join();
    if (th_frame_.joinable()) th_frame_.join();
    if (th_track_.joinable()) th_track_.join();
}

void Server::frame_cache_loop() {
    Frame f;
    while (running_.load()) {
        if (!pipe_.frames_ws->pop(f, 100)) continue;
        std::lock_guard<std::mutex> g(frame_mu_);
        latest_frame_ = std::move(f);
        has_frame_ = true;
    }
}

void Server::track_cache_loop() {
    TrackingResult tr;
    while (running_.load()) {
        if (!pipe_.localized_srv->pop(tr, 100)) continue;
        std::lock_guard<std::mutex> g(track_mu_);
        latest_tracks_ = std::move(tr);
        ++track_version_;
    }
}

void Server::http_loop() {
    httplib::Server svr;
    server_handle_ = &svr;

    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET,POST,OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"},
    });

    svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    // health
    svr.Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
        json j = {
            {"ok", true},
            {"ts_ms", now_ms()},
            {"camera_connected", stats_.camera_connected ? stats_.camera_connected() : false}
        };
        res.set_content(j.dump(), "application/json");
    });

    // stats
    svr.Get("/api/stats", [this](const httplib::Request&, httplib::Response& res) {
        json j = {
            {"camera", {
                {"connected", stats_.camera_connected ? stats_.camera_connected() : false},
                {"fps",       stats_.camera_fps ? stats_.camera_fps() : 0.f},
                {"seq",       stats_.camera_seq ? stats_.camera_seq() : 0}
            }},
            {"inference", {
                {"count",      stats_.infer_count ? stats_.infer_count() : 0},
                {"latency_ms", stats_.infer_lat_ms ? stats_.infer_lat_ms() : 0.f}
            }},
            {"tracker", {
                {"active", stats_.active_tracks ? stats_.active_tracks() : 0}
            }},
            {"storage", {
                {"rows", stats_.rows_stored ? stats_.rows_stored() : 0}
            }},
            {"pose_known", localization::PoseStore::instance().has()}
        };
        res.set_content(j.dump(), "application/json");
    });

    // current pose
    svr.Get("/api/pose", [](const httplib::Request&, httplib::Response& res) {
        auto p = localization::PoseStore::instance().get();
        json j = {
            {"lat", p.lat}, {"lon", p.lon}, {"alt", p.alt},
            {"yaw", p.yaw}, {"pitch", p.pitch}, {"roll", p.roll},
            {"ts_ms", p.ts_ms}
        };
        res.set_content(j.dump(), "application/json");
    });

    // post pose
    svr.Post("/api/pose", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            CameraPose p;
            p.lat   = j.value("lat", 0.0);
            p.lon   = j.value("lon", 0.0);
            p.alt   = j.value("alt", 0.0);
            p.yaw   = j.value("yaw", 0.f);
            p.pitch = j.value("pitch", 0.f);
            p.roll  = j.value("roll", 0.f);
            p.ts_ms = j.value("ts_ms", now_ms());
            localization::PoseStore::instance().update(p);
            res.set_content(R"({"ok":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string(R"({"ok":false,"err":")") + e.what() + "\"}", "application/json");
        }
    });

    // camera device list (host enumeration; best-effort)
    svr.Get("/api/camera/devices", [this](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        if (stats_.camera_devices) {
            for (auto& d : stats_.camera_devices()) {
                arr.push_back({
                    {"id", d.id},
                    {"label", d.label},
                    {"kind", d.kind},
                    {"available", d.available}
                });
            }
        }
#if defined(__APPLE__)
        const char* platform = "macos";
#elif defined(__linux__)
        const char* platform = "linux";
#elif defined(_WIN32)
        const char* platform = "windows";
#else
        const char* platform = "unknown";
#endif
        // crude "are we inside a container?" signal — useful UI hint on macOS host
        bool in_container = std::filesystem::exists("/.dockerenv");
        json j = {
            {"devices", arr},
            {"current", stats_.camera_source ? stats_.camera_source() : ""},
            {"platform", platform},
            {"in_container", in_container}
        };
        res.set_content(j.dump(), "application/json");
    });

    // current camera source
    svr.Get("/api/camera/source", [this](const httplib::Request&, httplib::Response& res) {
        json j = {{"source", stats_.camera_source ? stats_.camera_source() : ""}};
        res.set_content(j.dump(), "application/json");
    });

    // switch camera source at runtime
    svr.Post("/api/camera/source", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string src = j.value("source", std::string{});
            if (src.empty()) {
                res.status = 400;
                res.set_content(R"({"ok":false,"err":"source required"})", "application/json");
                return;
            }
            bool ok = stats_.camera_set_source ? stats_.camera_set_source(src) : false;
            json out = {{"ok", ok}, {"source", src}};
            res.set_content(out.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string(R"({"ok":false,"err":")") + e.what() + "\"}", "application/json");
        }
    });

    // recent tracks
    svr.Get("/api/tracks", [this](const httplib::Request& req, httplib::Response& res) {
        int limit = 200;
        if (req.has_param("limit")) limit = std::atoi(req.get_param_value("limit").c_str());
        json arr = json::array();
        if (store_) for (auto& r : store_->recent(limit)) arr.push_back(row_to_json(r));
        res.set_content(arr.dump(), "application/json");
    });

    // tracks by id
    svr.Get(R"(/api/tracks/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        int id = std::atoi(req.matches[1].str().c_str());
        json arr = json::array();
        if (store_) for (auto& r : store_->by_object(id)) arr.push_back(row_to_json(r));
        res.set_content(arr.dump(), "application/json");
    });

    // latest live snapshot (in-memory) — fast endpoint, no DB hit
    svr.Get("/api/live", [this](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        {
            std::lock_guard<std::mutex> g(track_mu_);
            for (auto& o : latest_tracks_.objects) arr.push_back(track_to_json(o));
        }
        res.set_content(arr.dump(), "application/json");
    });

    // SSE events — pushes latest tracks ~10Hz
    svr.Get("/api/events", [this](const httplib::Request&, httplib::Response& res) {
        res.set_chunked_content_provider(
            "text/event-stream",
            [this](size_t /*offset*/, httplib::DataSink& sink) {
                int last_ver = -1;
                int idle = 0;
                while (running_.load() && sink.is_writable()) {
                    int ver = 0;
                    json arr = json::array();
                    {
                        std::lock_guard<std::mutex> g(track_mu_);
                        ver = track_version_;
                        if (ver != last_ver) {
                            for (auto& o : latest_tracks_.objects) arr.push_back(track_to_json(o));
                        }
                    }
                    if (ver != last_ver) {
                        last_ver = ver;
                        idle = 0;
                        std::string payload = "data: " + arr.dump() + "\n\n";
                        if (!sink.write(payload.data(), payload.size())) break;
                    } else if (++idle > 20) {
                        const char* ping = ": ping\n\n";
                        if (!sink.write(ping, 8)) break;
                        idle = 0;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                sink.done();
                return true;
            });
    });

    // current frame as JPEG (when OpenCV available); else PPM-like raw blob
    svr.Get("/api/frame.jpg", [this](const httplib::Request&, httplib::Response& res) {
        Frame f;
        bool have = false;
        {
            std::lock_guard<std::mutex> g(frame_mu_);
            if (has_frame_) { f = latest_frame_; have = true; }
        }
        if (!have) { res.status = 503; res.set_content("no frame yet", "text/plain"); return; }
#ifdef OSCAR_HAVE_OPENCV
        cv::Mat m(f.height, f.width, CV_8UC3, f.data.data());
        std::vector<uchar> buf;
        cv::imencode(".jpg", m, buf, {cv::IMWRITE_JPEG_QUALITY, 75});
        res.set_content(std::string(buf.begin(), buf.end()), "image/jpeg");
#else
        // fallback: raw BGR header + bytes — clients hit /api/frame.raw
        res.set_header("X-Frame-Width",  std::to_string(f.width));
        res.set_header("X-Frame-Height", std::to_string(f.height));
        res.set_content(std::string(f.data.begin(), f.data.end()), "application/octet-stream");
#endif
    });

    // static web root
    if (!cfg_.web_root.empty()) {
        svr.set_mount_point("/", cfg_.web_root);
    }

    LOG_INFO("server", "HTTP listening on http://", cfg_.bind, ":", cfg_.port);
    svr.listen(cfg_.bind.c_str(), cfg_.port);
    LOG_INFO("server", "HTTP loop exit");
    server_handle_ = nullptr;
}

} // namespace oscar::server
