#include "common/Config.hpp"
#include "common/Logger.hpp"
#include "common/Pipeline.hpp"
#include "camera/CameraSource.hpp"
#include "inference/Detector.hpp"
#include "tracking/Tracker.hpp"
#include "localization/Localizer.hpp"
#include "storage/Storage.hpp"
#include "server/Server.hpp"

#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <iostream>
#include <string>

namespace {
std::atomic<bool> g_stop{false};
void on_sig(int) { g_stop = true; }
}

int main(int argc, char** argv) {
    std::string cfg_path = "config/oscar.json";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) cfg_path = argv[++i];
        else if (a == "--help" || a == "-h") {
            std::cout << "OSCAR core engine\n"
                      << "Usage: " << argv[0] << " [--config <path>]\n";
            return 0;
        }
    }

    std::signal(SIGINT,  on_sig);
    std::signal(SIGTERM, on_sig);

    auto cfg = oscar::load_config(cfg_path);
    LOG_INFO("main", "loaded config: ", cfg_path);
    LOG_INFO("main", "camera src=", cfg.camera.source, " infer=", cfg.inference.model_path,
                    " stub=", cfg.inference.stub_mode, " http=", cfg.server.bind, ":", cfg.server.port);

    auto pipe = oscar::Pipeline::make();

    oscar::camera::CameraSource camera(cfg.camera, pipe);
    oscar::inference::Detector  detector(cfg.inference, pipe);
    oscar::tracking::Tracker    tracker(cfg.tracker, pipe);
    oscar::localization::Localizer localizer(cfg.localization, pipe);
    oscar::storage::Storage     storage(cfg.storage, pipe);

    oscar::server::AppStats stats;
    stats.camera_connected = [&]{ return camera.connected(); };
    stats.camera_fps       = [&]{ return camera.fps(); };
    stats.camera_seq       = [&]{ return camera.seq(); };
    stats.infer_count      = [&]{ return detector.infer_count(); };
    stats.infer_lat_ms     = [&]{ return detector.avg_latency_ms(); };
    stats.active_tracks    = [&]{ return tracker.active_tracks(); };
    stats.rows_stored      = [&]{ return storage.total_rows(); };
    stats.camera_devices   = []{
        std::vector<oscar::server::CameraDeviceDTO> out;
        for (auto& d : oscar::camera::CameraSource::enumerate()) {
            out.push_back({d.id, d.label, d.kind, d.available});
        }
        return out;
    };
    stats.camera_source    = [&]{ return camera.current_source(); };
    stats.camera_set_source = [&](const std::string& s){ camera.set_source(s); return true; };

    oscar::server::Server server(cfg.server, pipe, &storage, stats);

    storage.start();
    localizer.start();
    tracker.start();
    detector.start();
    server.start();
    camera.start();

    LOG_INFO("main", "OSCAR running. Ctrl+C to stop.");
    while (!g_stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));

    LOG_INFO("main", "stopping...");
    camera.stop();
    detector.stop();
    tracker.stop();
    localizer.stop();
    pipe.close_all();
    server.stop();
    storage.stop();
    LOG_INFO("main", "bye");
    return 0;
}
