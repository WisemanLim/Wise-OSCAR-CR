#pragma once
#include "Types.hpp"
#include "Queue.hpp"
#include <memory>

namespace oscar {

// Shared pipeline channels between layers.
struct Pipeline {
    std::shared_ptr<BoundedQueue<Frame>>            frames;       // camera -> inference
    std::shared_ptr<BoundedQueue<Frame>>            frames_rec;   // camera -> storage
    std::shared_ptr<BoundedQueue<Frame>>            frames_ws;    // camera -> websocket
    std::shared_ptr<BoundedQueue<DetectionResult>>  detections;   // inference -> tracker
    std::shared_ptr<BoundedQueue<TrackingResult>>   tracks;       // tracker -> localization
    std::shared_ptr<BoundedQueue<TrackingResult>>   localized;    // localization -> storage
    std::shared_ptr<BoundedQueue<TrackingResult>>   localized_srv;// localization -> server (SSE)

    static Pipeline make() {
        Pipeline p;
        p.frames     = std::make_shared<BoundedQueue<Frame>>(4);
        p.frames_rec = std::make_shared<BoundedQueue<Frame>>(8);
        p.frames_ws  = std::make_shared<BoundedQueue<Frame>>(2);
        p.detections = std::make_shared<BoundedQueue<DetectionResult>>(8);
        p.tracks     = std::make_shared<BoundedQueue<TrackingResult>>(8);
        p.localized     = std::make_shared<BoundedQueue<TrackingResult>>(32);
        p.localized_srv = std::make_shared<BoundedQueue<TrackingResult>>(32);
        return p;
    }

    void close_all() {
        frames->close(); frames_rec->close(); frames_ws->close();
        detections->close(); tracks->close();
        localized->close(); localized_srv->close();
    }
};

} // namespace oscar
