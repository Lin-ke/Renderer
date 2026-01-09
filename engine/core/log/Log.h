#ifndef LOG_H
#define LOG_H

#include <format>
#include <glog/logging.h>
#include <string>
#include "engine/core/os/thread_pool.h"

// Custom Log Sink for GLog
class CustomLogSink : public google::LogSink {
public:
    void send(google::LogSeverity severity, const char* full_filename,
              const char* base_filename, int line,
              const struct tm* tm_time,
              const char* message, size_t message_len) override;

    void WaitTillSent() override {
        // No-op for synchronous output
    }
};


#define DECLARE_LOG_TAG(TagName) extern const char* TagName
#define DEFINE_LOG_TAG(TagName, DisplayString) const char* TagName = DisplayString

// ==========================================
// Part 2: Log 类 (底层实现)
// ==========================================
class Log {
public:
    static void init(); // 实现请放在 .cpp
    static void shutdown();

    // 这里接收的是 const char* (即全局变量的值)
    template<typename... Args>
    static void info(const char* file, int line, int thread_id, const char* tag, std::format_string<Args...> fmt, Args&&... args) {
        google::LogMessage(file, line, google::GLOG_INFO).stream() 
            << "[" << tag << "] "
            << std::format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void warn(const char* file, int line, int thread_id, const char* tag, std::format_string<Args...> fmt, Args&&... args) {
        google::LogMessage(file, line, google::GLOG_WARNING).stream() 
            << "[" << tag << "] "
            << std::format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void error(const char* file, int line, int thread_id, const char* tag, std::format_string<Args...> fmt, Args&&... args) {
        google::LogMessage(file, line, google::GLOG_ERROR).stream() 
            << "[" << tag << "] "
            << std::format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void critical(const char* file, int line, int thread_id, const char* tag, std::format_string<Args...> fmt, Args&&... args) {
        google::LogMessage(file, line, google::GLOG_FATAL).stream() 
            << "[" << tag << "] "
            << std::format(fmt, std::forward<Args>(args)...);
    }
};

// ==========================================
// Part 3: 使用宏
// ==========================================

// 清理旧宏
#ifdef INFO
#undef INFO
#endif
#ifdef WARN
#undef WARN
#endif
#ifdef ERR
#undef ERR
#endif
#ifdef FATAL
#undef FATAL
#endif

// 现在的用法变成了 INFO(LogTagName, "Message")
#define INFO(Tag, ...)   ::Log::info(__FILE__, __LINE__, ThreadPool::get_thread_id(), Tag, __VA_ARGS__)
#define WARN(Tag, ...)   ::Log::warn(__FILE__, __LINE__, ThreadPool::get_thread_id(), Tag, __VA_ARGS__)
#define ERR(Tag, ...)    ::Log::error(__FILE__, __LINE__, ThreadPool::get_thread_id(), Tag, __VA_ARGS__)
#define FATAL(Tag, ...)  ::Log::critical(__FILE__, __LINE__, ThreadPool::get_thread_id(), Tag, __VA_ARGS__)

#endif // LOG_H