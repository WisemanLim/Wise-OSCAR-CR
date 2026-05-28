#pragma once
#include "../common/Types.hpp"
#include "../common/Config.hpp"
#include "../common/Pipeline.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <memory>

namespace oscar::inference {

class IBackend;

class Detector {
public:
    Detector(InferenceCfg cfg, Pipeline pipe);
    ~Detector();

    void start();
    void stop();
    int64_t infer_count() const { return infer_count_.load(); }
    float avg_latency_ms() const { return avg_lat_ms_.load(); }

private:
    void run();

    InferenceCfg cfg_;
    Pipeline pipe_;
    std::atomic<bool> running_{false};
    std::atomic<int64_t> infer_count_{0};
    std::atomic<float> avg_lat_ms_{0.f};
    std::thread th_;
    std::unique_ptr<IBackend> backend_;
    std::vector<std::string> labels_;
};

} // namespace oscar::inference
