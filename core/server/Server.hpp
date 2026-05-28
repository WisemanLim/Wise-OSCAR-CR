#pragma once
#include "../common/Types.hpp"
#include "../common/Config.hpp"
#include "../common/Pipeline.hpp"
#include "../storage/Storage.hpp"
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <functional>
#include <memory>

namespace oscar::server {

struct CameraDeviceDTO {
    std::string id;
    std::string label;
    std::string kind;
    bool available{true};
};

struct AppStats {
    std::function<bool()>   camera_connected;
    std::function<float()>  camera_fps;
    std::function<int()>    camera_seq;
    std::function<int64_t()> infer_count;
    std::function<float()>  infer_lat_ms;
    std::function<size_t()> active_tracks;
    std::function<int64_t()> rows_stored;

    // camera control
    std::function<std::vector<CameraDeviceDTO>()> camera_devices;
    std::function<std::string()>                  camera_source;
    std::function<bool(const std::string&)>       camera_set_source;
};

class Server {
public:
    Server(ServerCfg cfg, Pipeline pipe, storage::Storage* store, AppStats stats);
    ~Server();
    void start();
    void stop();

private:
    void http_loop();
    void frame_cache_loop();
    void track_cache_loop();

    ServerCfg cfg_;
    Pipeline pipe_;
    storage::Storage* store_;
    AppStats stats_;
    std::atomic<bool> running_{false};

    std::thread th_http_;
    std::thread th_frame_;
    std::thread th_track_;

    std::mutex frame_mu_;
    Frame latest_frame_;
    bool has_frame_{false};

    std::mutex track_mu_;
    TrackingResult latest_tracks_;
    int track_version_{0};

    // SSE subscribers
    struct Sub { int last_version{0}; };
    void* server_handle_{nullptr};
};

} // namespace oscar::server
