#pragma once
#include "../common/Types.hpp"
#include "../common/Config.hpp"
#include "../common/Pipeline.hpp"
#include "../common/Logger.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace oscar::camera {

struct CameraDevice {
    std::string id;        // value to pass back as `source` ("0", "/dev/video0", "rtsp://...")
    std::string label;     // human-readable
    std::string kind;      // "index" | "device" | "url"
    bool available{true};  // probed-open succeeded (best-effort)
};

class CameraSource {
public:
    CameraSource(CameraCfg cfg, Pipeline pipe);
    ~CameraSource();

    void start();
    void stop();
    bool connected() const { return connected_.load(); }
    float fps() const { return fps_.load(); }
    int seq() const { return seq_.load(); }

    // runtime source change — thread-safe, reopens on next loop tick
    void set_source(const std::string& src);
    std::string current_source() const;

    // best-effort host camera enumeration
    static std::vector<CameraDevice> enumerate();

private:
    void run();
    void synthetic_frame_loop();
    bool try_open();

    CameraCfg cfg_;
    Pipeline pipe_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> need_reopen_{false};
    std::atomic<float> fps_{0.f};
    std::atomic<int> seq_{0};
    mutable std::mutex cfg_mu_;
    std::thread th_;
};

} // namespace oscar::camera
