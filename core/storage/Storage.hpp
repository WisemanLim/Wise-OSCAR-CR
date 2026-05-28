#pragma once
#include "../common/Types.hpp"
#include "../common/Config.hpp"
#include "../common/Pipeline.hpp"
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>
#include <string>
#include <vector>

struct sqlite3;

namespace oscar::storage {

struct TrackRow {
    int64_t ts_ms;
    int object_id;
    std::string category;
    std::string state;
    float bbox_x, bbox_y, bbox_w, bbox_h;
    float rel_x, rel_y, rel_z;
    float abs_x, abs_y, abs_z;
    float score;
    std::string class_name;
};

class Storage {
public:
    Storage(StorageCfg cfg, Pipeline pipe);
    ~Storage();
    void start();
    void stop();

    // Sync API for HTTP layer
    std::vector<TrackRow> recent(int limit = 200);
    std::vector<TrackRow> by_object(int id, int limit = 200);
    int64_t total_rows();

private:
    void track_writer_loop();
    void video_writer_loop();
    void open_db();

    StorageCfg cfg_;
    Pipeline pipe_;
    std::atomic<bool> running_{false};
    std::thread th_tracks_;
    std::thread th_video_;

    sqlite3* db_{nullptr};
    std::mutex db_mu_;
};

} // namespace oscar::storage
