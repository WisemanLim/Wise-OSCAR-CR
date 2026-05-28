#pragma once
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <optional>

namespace oscar {

// Bounded thread-safe queue. Drop-oldest on overflow to keep latency low.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity = 4) : cap_(capacity) {}

    void push(T v) {
        std::lock_guard<std::mutex> g(mu_);
        if (q_.size() >= cap_) q_.pop_front();
        q_.push_back(std::move(v));
        cv_.notify_one();
    }

    bool pop(T& out, int timeout_ms = 100) {
        std::unique_lock<std::mutex> lk(mu_);
        if (!cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                          [&]{ return !q_.empty() || closed_; })) {
            return false;
        }
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> g(mu_);
        closed_ = true;
        cv_.notify_all();
    }

    size_t size() {
        std::lock_guard<std::mutex> g(mu_);
        return q_.size();
    }

private:
    size_t cap_;
    std::deque<T> q_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool closed_{false};
};

} // namespace oscar
