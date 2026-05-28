#include "Tracker.hpp"
#include "../common/Logger.hpp"
#include <algorithm>
#include <vector>

namespace oscar::tracking {

static float iou(const BBox& a, const BBox& b) {
    float x1 = std::max(a.x, b.x);
    float y1 = std::max(a.y, b.y);
    float x2 = std::min(a.x + a.w, b.x + b.w);
    float y2 = std::min(a.y + a.h, b.y + b.h);
    float iw = std::max(0.f, x2 - x1);
    float ih = std::max(0.f, y2 - y1);
    float inter = iw * ih;
    float uni = a.w * a.h + b.w * b.h - inter;
    if (uni <= 0) return 0.f;
    return inter / uni;
}

static const char* category_of(int class_id, const std::string& name) {
    static const std::vector<std::string> persons = {"person"};
    static const std::vector<std::string> vehicles = {"car","truck","bus","motorcycle","bicycle","train","boat","airplane"};
    static const std::vector<std::string> animals  = {"cat","dog","horse","sheep","cow","bird","bear","zebra","giraffe"};
    auto in = [&](const std::vector<std::string>& v){ return std::find(v.begin(), v.end(), name) != v.end(); };
    if (in(persons))  return "person";
    if (in(vehicles)) return "vehicle";
    if (in(animals))  return "animal";
    (void)class_id;
    return "object";
}

Tracker::Tracker(TrackerCfg cfg, Pipeline pipe)
    : cfg_(std::move(cfg)), pipe_(std::move(pipe)) {}

Tracker::~Tracker() { stop(); }

void Tracker::start() {
    if (running_.exchange(true)) return;
    th_ = std::thread([this]{ run(); });
}

void Tracker::stop() {
    if (!running_.exchange(false)) return;
    if (th_.joinable()) th_.join();
}

void Tracker::update(const DetectionResult& dr, TrackingResult& out) {
    // 1) Greedy IoU matching detections -> existing tracks
    std::vector<int> det_assigned(dr.boxes.size(), -1);
    std::vector<int> trk_ids; trk_ids.reserve(tracks_.size());
    for (auto& kv : tracks_) trk_ids.push_back(kv.first);

    std::vector<int> trk_match(trk_ids.size(), -1);

    for (size_t ti = 0; ti < trk_ids.size(); ++ti) {
        float best_iou = cfg_.match_thresh > 0 ? (1.f - cfg_.match_thresh) : 0.3f;
        int best_di = -1;
        const Track& t = tracks_[trk_ids[ti]];
        for (size_t di = 0; di < dr.boxes.size(); ++di) {
            if (det_assigned[di] != -1) continue;
            float v = iou(t.bbox, dr.boxes[di]);
            if (v > best_iou) { best_iou = v; best_di = int(di); }
        }
        if (best_di >= 0) {
            trk_match[ti] = best_di;
            det_assigned[best_di] = trk_ids[ti];
        }
    }

    // 2) Update matched tracks
    for (size_t ti = 0; ti < trk_ids.size(); ++ti) {
        int tid = trk_ids[ti];
        Track& t = tracks_[tid];
        if (trk_match[ti] >= 0) {
            const BBox& nb = dr.boxes[trk_match[ti]];
            Point2D cur{ nb.x + nb.w / 2.f, nb.y + nb.h / 2.f };
            if (!t.recent_centroids.empty()) {
                auto& prev = t.recent_centroids.back();
                t.last_velocity_px = { cur.x - prev.x, cur.y - prev.y };
            }
            t.recent_centroids.push_back(cur);
            while (t.recent_centroids.size() > 30) t.recent_centroids.pop_front();
            t.bbox = nb;
            t.hits++;
            t.misses = 0;
            t.last_ts = dr.ts_ms;
        } else {
            t.misses++;
        }
    }

    // 3) Spawn new tracks for unassigned detections above det_thresh
    for (size_t di = 0; di < dr.boxes.size(); ++di) {
        if (det_assigned[di] != -1) continue;
        if (dr.boxes[di].score < cfg_.det_thresh) continue;
        Track t;
        t.id = next_id_++;
        t.bbox = dr.boxes[di];
        t.hits = 1;
        t.misses = 0;
        t.last_ts = dr.ts_ms;
        t.recent_centroids.push_back({t.bbox.x + t.bbox.w/2, t.bbox.y + t.bbox.h/2});
        tracks_[t.id] = std::move(t);
    }

    // 4) Drop stale tracks
    for (auto it = tracks_.begin(); it != tracks_.end(); ) {
        if (it->second.misses > cfg_.track_buffer) it = tracks_.erase(it);
        else ++it;
    }

    // 5) Emit
    out.ts_ms = dr.ts_ms;
    out.frame_seq = dr.frame_seq;
    out.objects.clear();
    out.objects.reserve(tracks_.size());
    for (auto& kv : tracks_) {
        if (kv.second.misses > 0) continue;
        TrackedObject o;
        o.id = kv.first;
        o.bbox = kv.second.bbox;
        o.velocity_px = kv.second.last_velocity_px;
        o.ts_ms = dr.ts_ms;
        o.category = category_of(o.bbox.class_id, o.bbox.class_name);
        // state filled by Localization layer
        out.objects.push_back(std::move(o));
    }
    active_.store(out.objects.size());
}

void Tracker::run() {
    DetectionResult dr;
    while (running_.load()) {
        if (!pipe_.detections->pop(dr, 100)) continue;
        TrackingResult tr;
        update(dr, tr);
        pipe_.tracks->push(std::move(tr));
    }
}

} // namespace oscar::tracking
