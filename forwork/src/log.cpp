#include "log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <functional>
#include <thread>

namespace logging {

static const char* levelName(Level lvl) {
    switch (lvl) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?????";
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setLevel(Level lvl) {
    level_ = lvl;
}

void Logger::log(Level lvl, const char* fmt, ...) {
    if (lvl < level_) return;

    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    // 时间戳 HH:MM:SS.mmm
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()).count();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
    localtime_s(&tmv, &t);
    char timeBuf[32];
    std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d.%03lld",
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                  static_cast<long long>(ms % 1000));

    std::lock_guard<std::mutex> lk(mutex_);
    std::fprintf(stderr, "[%s][%s][tid %u] %s\n",
                 timeBuf, levelName(lvl),
                 static_cast<unsigned int>(
                     std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xffff),
                 msg);
}

}  // namespace logging
