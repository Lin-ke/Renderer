#ifndef LOG_H
#define LOG_H

#include <format>
#include <glog/logging.h>

class Log {
public:
    static void init();

    template<typename... Args>
    static void info(const char* file, int line, std::format_string<Args...> fmt, Args&&... args) {
        google::LogMessage(file, line, google::GLOG_INFO).stream() 
            << std::format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void warn(const char* file, int line, std::format_string<Args...> fmt, Args&&... args) {
        google::LogMessage(file, line, google::GLOG_WARNING).stream() 
            << std::format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void error(const char* file, int line, std::format_string<Args...> fmt, Args&&... args) {
        google::LogMessage(file, line, google::GLOG_ERROR).stream() 
            << std::format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void critical(const char* file, int line, std::format_string<Args...> fmt, Args&&... args) {
        google::LogMessage(file, line, google::GLOG_FATAL).stream() 
            << std::format(fmt, std::forward<Args>(args)...);
    }
};

#ifdef INFO
#undef INFO
#endif
#ifdef WARN
#undef WARN
#endif

#define INFO(...)   ::Log::info(__FILE__, __LINE__, __VA_ARGS__)
#define WARN(...)   ::Log::warn(__FILE__, __LINE__, __VA_ARGS__)
#define ERR(...)    ::Log::error(__FILE__, __LINE__, __VA_ARGS__)
#define FATAL(...)  ::Log::critical(__FILE__, __LINE__, __VA_ARGS__)

#endif // LOG_H