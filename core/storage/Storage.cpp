#include "Storage.hpp"
#include "../common/Logger.hpp"

#include <sqlite3.h>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#ifdef OSCAR_HAVE_OPENCV
#include <opencv2/opencv.hpp>
#endif

namespace fs = std::filesystem;

namespace oscar::storage {

static std::string current_segment_name(const std::string& dir) {
    auto t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << dir << "/oscar_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".mp4";
    return oss.str();
}

Storage::Storage(StorageCfg cfg, Pipeline pipe)
    : cfg_(std::move(cfg)), pipe_(std::move(pipe)) {}

Storage::~Storage() { stop(); if (db_) sqlite3_close(db_); }

void Storage::open_db() {
    fs::create_directories(fs::path(cfg_.db_path).parent_path());
    if (sqlite3_open(cfg_.db_path.c_str(), &db_) != SQLITE_OK) {
        LOG_ERR("storage", "sqlite open failed: ", sqlite3_errmsg(db_));
        return;
    }
    const char* sql =
        "PRAGMA journal_mode=WAL;"
        "CREATE TABLE IF NOT EXISTS tracks ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ts_ms INTEGER NOT NULL,"
        "  object_id INTEGER NOT NULL,"
        "  category TEXT,"
        "  state TEXT,"
        "  class_name TEXT,"
        "  score REAL,"
        "  bbox_x REAL, bbox_y REAL, bbox_w REAL, bbox_h REAL,"
        "  rel_x REAL, rel_y REAL, rel_z REAL,"
        "  abs_x REAL, abs_y REAL, abs_z REAL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_tracks_obj ON tracks(object_id);"
        "CREATE INDEX IF NOT EXISTS idx_tracks_ts  ON tracks(ts_ms);"
        "CREATE TABLE IF NOT EXISTS videos ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  path TEXT NOT NULL,"
        "  start_ts INTEGER, end_ts INTEGER,"
        "  frame_count INTEGER"
        ");";
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        LOG_ERR("storage", "sqlite schema: ", err ? err : "?");
        sqlite3_free(err);
    }
}

void Storage::start() {
    open_db();
    if (running_.exchange(true)) return;
    th_tracks_ = std::thread([this]{ track_writer_loop(); });
    if (cfg_.enable_video)
        th_video_ = std::thread([this]{ video_writer_loop(); });
}

void Storage::stop() {
    if (!running_.exchange(false)) return;
    if (th_tracks_.joinable()) th_tracks_.join();
    if (th_video_.joinable())  th_video_.join();
}

void Storage::track_writer_loop() {
    const char* ins =
        "INSERT INTO tracks (ts_ms,object_id,category,state,class_name,score,"
        "bbox_x,bbox_y,bbox_w,bbox_h,rel_x,rel_y,rel_z,abs_x,abs_y,abs_z)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";

    TrackingResult tr;
    while (running_.load()) {
        if (!pipe_.localized->pop(tr, 100)) continue;
        std::lock_guard<std::mutex> g(db_mu_);
        if (!db_) continue;
        sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, nullptr);
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_, ins, -1, &st, nullptr) != SQLITE_OK) {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            continue;
        }
        for (auto& o : tr.objects) {
            sqlite3_bind_int64(st, 1, o.ts_ms);
            sqlite3_bind_int  (st, 2, o.id);
            sqlite3_bind_text (st, 3, o.category.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text (st, 4, o.state.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text (st, 5, o.bbox.class_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(st, 6, o.bbox.score);
            sqlite3_bind_double(st, 7, o.bbox.x);
            sqlite3_bind_double(st, 8, o.bbox.y);
            sqlite3_bind_double(st, 9, o.bbox.w);
            sqlite3_bind_double(st,10, o.bbox.h);
            sqlite3_bind_double(st,11, o.rel_pos.x);
            sqlite3_bind_double(st,12, o.rel_pos.y);
            sqlite3_bind_double(st,13, o.rel_pos.z);
            sqlite3_bind_double(st,14, o.abs_pos.x);
            sqlite3_bind_double(st,15, o.abs_pos.y);
            sqlite3_bind_double(st,16, o.abs_pos.z);
            sqlite3_step(st);
            sqlite3_reset(st);
        }
        sqlite3_finalize(st);
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    }
}

void Storage::video_writer_loop() {
    fs::create_directories(cfg_.video_dir);
#ifdef OSCAR_HAVE_OPENCV
    cv::VideoWriter writer;
    std::string path;
    int64_t seg_start = 0;
    int seg_frames = 0;
    int fps = 30;
    int W = 0, H = 0;

    auto roll = [&](int w, int h) {
        if (writer.isOpened()) {
            writer.release();
            std::lock_guard<std::mutex> g(db_mu_);
            if (db_) {
                sqlite3_stmt* st = nullptr;
                sqlite3_prepare_v2(db_,
                    "INSERT INTO videos(path,start_ts,end_ts,frame_count) VALUES(?,?,?,?);",
                    -1, &st, nullptr);
                sqlite3_bind_text (st, 1, path.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(st, 2, seg_start);
                sqlite3_bind_int64(st, 3, now_ms());
                sqlite3_bind_int  (st, 4, seg_frames);
                sqlite3_step(st);
                sqlite3_finalize(st);
            }
        }
        path = current_segment_name(cfg_.video_dir);
        int fourcc = cv::VideoWriter::fourcc('m','p','4','v');
        writer.open(path, fourcc, fps, cv::Size(w, h));
        seg_start = now_ms();
        seg_frames = 0;
        W = w; H = h;
        LOG_INFO("storage", "video segment: ", path);
    };

    Frame f;
    while (running_.load()) {
        if (!pipe_.frames_rec->pop(f, 100)) continue;
        if (!writer.isOpened() || W != f.width || H != f.height) roll(f.width, f.height);
        if ((now_ms() - seg_start) > cfg_.segment_seconds * 1000) roll(f.width, f.height);
        cv::Mat m(f.height, f.width, CV_8UC3, f.data.data());
        writer.write(m);
        ++seg_frames;
    }
    if (writer.isOpened()) writer.release();
#else
    // No OpenCV: just drain queue + log periodically
    Frame f;
    int cnt = 0;
    while (running_.load()) {
        if (!pipe_.frames_rec->pop(f, 200)) continue;
        if ((++cnt % 100) == 0) LOG_INFO("storage", "video disabled (no OpenCV): drained ", cnt, " frames");
    }
#endif
}

std::vector<TrackRow> Storage::recent(int limit) {
    std::lock_guard<std::mutex> g(db_mu_);
    std::vector<TrackRow> out;
    if (!db_) return out;
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT ts_ms,object_id,category,state,class_name,score,"
        "bbox_x,bbox_y,bbox_w,bbox_h,rel_x,rel_y,rel_z,abs_x,abs_y,abs_z "
        "FROM tracks ORDER BY id DESC LIMIT ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int(st, 1, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        TrackRow r;
        r.ts_ms      = sqlite3_column_int64(st, 0);
        r.object_id  = sqlite3_column_int  (st, 1);
        r.category   = (const char*)sqlite3_column_text(st, 2);
        r.state      = (const char*)sqlite3_column_text(st, 3);
        auto cn      = sqlite3_column_text(st, 4);
        r.class_name = cn ? (const char*)cn : "";
        r.score      = (float)sqlite3_column_double(st, 5);
        r.bbox_x     = (float)sqlite3_column_double(st, 6);
        r.bbox_y     = (float)sqlite3_column_double(st, 7);
        r.bbox_w     = (float)sqlite3_column_double(st, 8);
        r.bbox_h     = (float)sqlite3_column_double(st, 9);
        r.rel_x      = (float)sqlite3_column_double(st,10);
        r.rel_y      = (float)sqlite3_column_double(st,11);
        r.rel_z      = (float)sqlite3_column_double(st,12);
        r.abs_x      = (float)sqlite3_column_double(st,13);
        r.abs_y      = (float)sqlite3_column_double(st,14);
        r.abs_z      = (float)sqlite3_column_double(st,15);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<TrackRow> Storage::by_object(int id, int limit) {
    std::lock_guard<std::mutex> g(db_mu_);
    std::vector<TrackRow> out;
    if (!db_) return out;
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT ts_ms,object_id,category,state,class_name,score,"
        "bbox_x,bbox_y,bbox_w,bbox_h,rel_x,rel_y,rel_z,abs_x,abs_y,abs_z "
        "FROM tracks WHERE object_id=? ORDER BY id DESC LIMIT ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int(st, 1, id);
    sqlite3_bind_int(st, 2, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        TrackRow r;
        r.ts_ms      = sqlite3_column_int64(st, 0);
        r.object_id  = sqlite3_column_int  (st, 1);
        r.category   = (const char*)sqlite3_column_text(st, 2);
        r.state      = (const char*)sqlite3_column_text(st, 3);
        auto cn      = sqlite3_column_text(st, 4);
        r.class_name = cn ? (const char*)cn : "";
        r.score      = (float)sqlite3_column_double(st, 5);
        r.bbox_x     = (float)sqlite3_column_double(st, 6);
        r.bbox_y     = (float)sqlite3_column_double(st, 7);
        r.bbox_w     = (float)sqlite3_column_double(st, 8);
        r.bbox_h     = (float)sqlite3_column_double(st, 9);
        r.rel_x      = (float)sqlite3_column_double(st,10);
        r.rel_y      = (float)sqlite3_column_double(st,11);
        r.rel_z      = (float)sqlite3_column_double(st,12);
        r.abs_x      = (float)sqlite3_column_double(st,13);
        r.abs_y      = (float)sqlite3_column_double(st,14);
        r.abs_z      = (float)sqlite3_column_double(st,15);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(st);
    return out;
}

int64_t Storage::total_rows() {
    std::lock_guard<std::mutex> g(db_mu_);
    if (!db_) return 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM tracks;", -1, &st, nullptr) != SQLITE_OK)
        return 0;
    int64_t n = 0;
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return n;
}

} // namespace oscar::storage
