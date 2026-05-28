#include "Localizer.hpp"
#include "../common/Logger.hpp"
#include <cmath>
#include <array>

namespace oscar::localization {

namespace {
constexpr double DEG2RAD = 3.14159265358979323846 / 180.0;
constexpr double EARTH_R = 6371000.0;

// flat-earth approx (good for < ~few km)
Point2D geodetic_to_local_m(double lat, double lon, double lat0, double lon0) {
    double dlat = (lat - lat0) * DEG2RAD;
    double dlon = (lon - lon0) * DEG2RAD;
    float nx = float(EARTH_R * dlon * std::cos(lat0 * DEG2RAD));   // east
    float ny = float(EARTH_R * dlat);                              // north
    return { nx, ny };
}

// 3x3 rotation: R = Rz(yaw) * Ry(pitch) * Rx(roll), rows applied to camera-frame vec
std::array<float,9> make_R(float yaw_deg, float pitch_deg, float roll_deg) {
    float y = yaw_deg   * float(DEG2RAD);
    float p = pitch_deg * float(DEG2RAD);
    float r = roll_deg  * float(DEG2RAD);
    float cy = std::cos(y), sy = std::sin(y);
    float cp = std::cos(p), sp = std::sin(p);
    float cr = std::cos(r), sr = std::sin(r);
    return {
        cy*cp,  cy*sp*sr - sy*cr,  cy*sp*cr + sy*sr,
        sy*cp,  sy*sp*sr + cy*cr,  sy*sp*cr - cy*sr,
          -sp,            cp*sr,            cp*cr
    };
}

Point3D rotate(const std::array<float,9>& R, Point3D v) {
    return {
        R[0]*v.x + R[1]*v.y + R[2]*v.z,
        R[3]*v.x + R[4]*v.y + R[5]*v.z,
        R[6]*v.x + R[7]*v.y + R[8]*v.z
    };
}
} // ns

Localizer::Localizer(LocalizationCfg cfg, Pipeline pipe)
    : cfg_(std::move(cfg)), pipe_(std::move(pipe)) {}

Localizer::~Localizer() { stop(); }

void Localizer::start() {
    if (running_.exchange(true)) return;
    th_ = std::thread([this]{ run(); });
}

void Localizer::stop() {
    if (!running_.exchange(false)) return;
    if (th_.joinable()) th_.join();
}

void Localizer::localize_one(TrackedObject& o, const CameraPose& cam) {
    // Use bottom-center of bbox as foot point (for ground-plane projection)
    float u = o.bbox.x + o.bbox.w / 2.f;
    float v = o.bbox.y + o.bbox.h;

    // K^-1 * [u,v,1] -> normalized ray in camera frame (x_right, y_down, z_forward)
    Point3D ray;
    ray.x = (u - cfg_.cx) / cfg_.fx;
    ray.y = (v - cfg_.cy) / cfg_.fy;
    ray.z = 1.0f;

    // Rotate by camera orientation
    auto R = make_R(cam.yaw, cam.pitch, cam.roll);
    Point3D world_ray = rotate(R, ray);

    // Ground-plane intersection: camera at height H above ground (Z=0 ground).
    // Solve for t where cam_z + t*ray_z = ground_height_m
    // Camera in world: position uses camera_height_m as Z for camera; ground = ground_height_m
    float Hcam = cfg_.camera_height_m;
    float Hg   = cfg_.ground_height_m;
    float dz   = world_ray.z;   // after rotation, this is vertical-ish component
    // The y component of camera-frame is down; after rotation depending on R the z may differ.
    // For robustness, project assuming pinhole+ground:
    float depth = 1.f;
    float drop = Hcam - Hg;
    if (std::abs(world_ray.y) > 1e-3f) {
        // simple flat camera (no rotation), use camera-frame y component as down
        depth = drop / std::max(0.05f, world_ray.y);
    }

    o.rel_pos.x = ray.x * depth;
    o.rel_pos.y = drop;
    o.rel_pos.z = ray.z * depth;

    // Camera world XY from geodetic. Use anchor = (cam.lat, cam.lon) so abs = cam_xy + rel rotated.
    static double anchor_lat = 0.0, anchor_lon = 0.0;
    static bool anchor_set = false;
    if (!anchor_set && (cam.lat != 0.0 || cam.lon != 0.0)) {
        anchor_lat = cam.lat;
        anchor_lon = cam.lon;
        anchor_set = true;
    }
    Point2D cam_xy = anchor_set ? geodetic_to_local_m(cam.lat, cam.lon, anchor_lat, anchor_lon)
                                : Point2D{0,0};
    // World offset from camera by rotating relative coords
    Point3D world_off = rotate(R, o.rel_pos);
    o.abs_pos.x = cam_xy.x + world_off.x;
    o.abs_pos.y = cam_xy.y + world_off.z;   // forward in world
    o.abs_pos.z = float(cam.alt) - world_off.y;

    // Track history & velocity in world
    auto& h = history_[o.id];
    h.abs_positions.push_back(o.abs_pos);
    h.ts_ms.push_back(o.ts_ms);
    while (h.abs_positions.size() > 30) { h.abs_positions.pop_front(); h.ts_ms.pop_front(); }

    float v_mag = 0.f;
    if (h.abs_positions.size() >= 2) {
        auto& p0 = h.abs_positions.front();
        auto& p1 = h.abs_positions.back();
        int64_t dt = h.ts_ms.back() - h.ts_ms.front();
        if (dt > 0) {
            float dx = p1.x - p0.x, dy = p1.y - p0.y;
            float dist = std::sqrt(dx*dx + dy*dy);
            v_mag = dist / (dt / 1000.f);
        }
    }
    o.state = (v_mag <= cfg_.static_velocity_thr) ? "Stationary" : "Moving";

    // Copy trajectory (recent abs positions)
    o.trajectory.assign(h.abs_positions.begin(), h.abs_positions.end());
}

void Localizer::run() {
    TrackingResult tr;
    while (running_.load()) {
        if (!pipe_.tracks->pop(tr, 100)) continue;
        CameraPose cam = PoseStore::instance().get();
        for (auto& o : tr.objects) localize_one(o, cam);
        pipe_.localized->push(tr);
        pipe_.localized_srv->push(tr);
    }
}

} // namespace oscar::localization
