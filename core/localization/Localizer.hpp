#pragma once
#include "../common/Types.hpp"
#include "../common/Config.hpp"
#include "../common/Pipeline.hpp"
#include "PoseStore.hpp"
#include <atomic>
#include <thread>
#include <unordered_map>
#include <deque>

namespace oscar::localization {

class Localizer {
public:
    Localizer(LocalizationCfg cfg, Pipeline pipe);
    ~Localizer();
    void start();
    void stop();

private:
    void run();
    void localize_one(TrackedObject& o, const CameraPose& cam);

    LocalizationCfg cfg_;
    Pipeline pipe_;
    std::atomic<bool> running_{false};
    std::thread th_;

    struct History {
        std::deque<Point3D> abs_positions;
        std::deque<int64_t> ts_ms;
    };
    std::unordered_map<int, History> history_;
};

} // namespace oscar::localization
