#pragma once

#include <format>
#include <glog/logging.h>

class Log {
public:
    static void init();

    template<typename... Args>
    static void info(std::format_string<Args...> fmt, Args&&... args) {
        LOG(INFO) << std::format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void warn(std::format_string<Args...> fmt, Args&&... args) {
        LOG(WARNING) << std::format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void error(std::format_string<Args...> fmt, Args&&... args) {
        LOG(ERROR) << std::format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void critical(std::format_string<Args...> fmt, Args&&... args) {
        LOG(FATAL) << std::format(fmt, std::forward<Args>(args)...);
    }
};

// Log macros adapted for the new Log class
#define INFO(...)    ::Log::info(__VA_ARGS__)
#define WARN(...)   ::Log::warn(__VA_ARGS__)
#define ERR(...)    ::Log::error(__VA_ARGS__)
#define FATAL(...)  ::Log::critical(__VA_ARGS__)
