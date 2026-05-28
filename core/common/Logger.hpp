#pragma once
#include <iostream>
#include <mutex>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace oscar {

class Logger {
public:
    enum class Level { DEBUG, INFO, WARN, ERROR };

    static Logger& instance() {
        static Logger l;
        return l;
    }

    void set_level(Level lv) { level_ = lv; }

    template <typename... Args>
    void log(Level lv, const std::string& tag, Args&&... args) {
        if (lv < level_) return;
        std::lock_guard<std::mutex> g(mu_);
        std::cerr << ts() << " [" << name(lv) << "] [" << tag << "] ";
        (std::cerr << ... << args);
        std::cerr << std::endl;
    }

private:
    Level level_{Level::INFO};
    std::mutex mu_;

    static std::string ts() {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto t = system_clock::to_time_t(now);
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
        std::ostringstream oss;
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        oss << std::put_time(&tm, "%H:%M:%S") << "." << std::setw(3) << std::setfill('0') << ms;
        return oss.str();
    }

    static const char* name(Level lv) {
        switch (lv) {
            case Level::DEBUG: return "DBG";
            case Level::INFO:  return "INF";
            case Level::WARN:  return "WRN";
            case Level::ERROR: return "ERR";
        }
        return "???";
    }
};

#define LOG_INFO(tag, ...)  ::oscar::Logger::instance().log(::oscar::Logger::Level::INFO,  tag, __VA_ARGS__)
#define LOG_WARN(tag, ...)  ::oscar::Logger::instance().log(::oscar::Logger::Level::WARN,  tag, __VA_ARGS__)
#define LOG_ERR(tag, ...)   ::oscar::Logger::instance().log(::oscar::Logger::Level::ERROR, tag, __VA_ARGS__)
#define LOG_DBG(tag, ...)   ::oscar::Logger::instance().log(::oscar::Logger::Level::DEBUG, tag, __VA_ARGS__)

} // namespace oscar
