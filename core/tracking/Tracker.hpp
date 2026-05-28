#pragma once
#include "../common/Types.hpp"
#include "../common/Config.hpp"
#include "../common/Pipeline.hpp"
#include <atomic>
#include <thread>
#include <unordered_map>
#include <deque>

namespace oscar::tracking {

struct Track {
    int id{0};
    BBox bbox;
    int hits{0};
    int misses{0};
    int64_t last_ts{0};
    std::deque<Point2D> recent_centroids;   // px space
    Point2D last_velocity_px{0,0};
};

class Tracker {
public:
    Tracker(TrackerCfg cfg, Pipeline pipe);
    ~Tracker();
    void start();
    void stop();
    size_t active_tracks() const { return active_.load(); }

private:
    void run();
    void update(const DetectionResult& dr, TrackingResult& out);

    TrackerCfg cfg_;
    Pipeline pipe_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> active_{0};
    std::thread th_;

    int next_id_{1};
    std::unordered_map<int, Track> tracks_;
};

} // namespace oscar::tracking
