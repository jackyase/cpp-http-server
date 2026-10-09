#pragma once

#include <mutex>
#include <string>

namespace logging {

enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3 };

// 线程安全日志（单例）
class Logger {
public:
    static Logger& instance();

    void setLevel(Level lvl);
    void log(Level lvl, const char* fmt, ...);  // printf 风格

private:
    Logger() = default;
    std::mutex mutex_;
    Level level_ = Level::Debug;
};

}  // namespace logging

#define LOG_DEBUG(...) ::logging::Logger::instance().log(::logging::Level::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::logging::Logger::instance().log(::logging::Level::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::logging::Logger::instance().log(::logging::Level::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::logging::Logger::instance().log(::logging::Level::Error, __VA_ARGS__)
