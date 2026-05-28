#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <array>

namespace oscar {

using Timestamp = std::chrono::system_clock::time_point;

inline int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

struct BBox {
    float x, y, w, h;    // pixel coords (top-left + size)
    float score{0.f};
    int class_id{-1};
    std::string class_name;
};

struct Point2D { float x{0}, y{0}; };
struct Point3D { float x{0}, y{0}, z{0}; };

struct CameraPose {
    double lat{0.0};
    double lon{0.0};
    double alt{0.0};
    float yaw{0.f};
    float pitch{0.f};
    float roll{0.f};
    int64_t ts_ms{0};
};

struct TrackedObject {
    int id{-1};
    BBox bbox;
    Point2D velocity_px;
    Point3D rel_pos;     // camera-relative
    Point3D abs_pos;     // world coords
    std::string state;   // "Moving" | "Stationary"
    std::string category;// "person" | "vehicle" | "animal" | "object"
    std::vector<Point3D> trajectory;
    int64_t ts_ms{0};
};

struct Frame {
    int64_t ts_ms{0};
    int width{0};
    int height{0};
    int channels{3};
    std::vector<uint8_t> data;   // BGR
    int seq{0};
};

struct DetectionResult {
    int64_t ts_ms{0};
    int frame_seq{0};
    std::vector<BBox> boxes;
};

struct TrackingResult {
    int64_t ts_ms{0};
    int frame_seq{0};
    std::vector<TrackedObject> objects;
};

} // namespace oscar
