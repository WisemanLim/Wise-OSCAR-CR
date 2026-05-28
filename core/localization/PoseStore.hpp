#pragma once
#include "../common/Types.hpp"
#include <atomic>
#include <mutex>

namespace oscar::localization {

class PoseStore {
public:
    static PoseStore& instance() { static PoseStore p; return p; }

    void update(const CameraPose& p) {
        std::lock_guard<std::mutex> g(mu_);
        cur_ = p;
        has_ = true;
    }

    CameraPose get() {
        std::lock_guard<std::mutex> g(mu_);
        return cur_;
    }

    bool has() const { return has_.load(); }

private:
    CameraPose cur_{};
    std::atomic<bool> has_{false};
    std::mutex mu_;
};

} // namespace oscar::localization
