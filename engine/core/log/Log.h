#pragma once

#include <format>
#include <glog/logging.h>
#include <string>
#include <iostream>
#include "engine/core/os/thread_pool.h"


class CustomLogSink : public google::LogSink {
public:
    void send(google::LogSeverity severity, const char* full_filename,
              const char* base_filename, int line,
              const struct tm* tm_time,
              const char* message, size_t message_len) override;

    void WaitTillSent() override {
        
    }
};


#define DECLARE_LOG_TAG(TagName) extern const char* TagName
#define DEFINE_LOG_TAG(TagName, DisplayString) const char* TagName = DisplayString


class Log {
public:
    static std::atomic<bool> initialized_;
    static std::mutex init_mutex_;

    static void init();
    static void shutdown();
    static void set_min_log_level(int level);

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

#define INFO(Tag, ...)   ::Log::info(__FILE__, __LINE__, ThreadPool::get_thread_id(), Tag, __VA_ARGS__)
#define WARN(Tag, ...)   ::Log::warn(__FILE__, __LINE__, ThreadPool::get_thread_id(), Tag, __VA_ARGS__)
#define ERR(Tag, ...)    ::Log::error(__FILE__, __LINE__, ThreadPool::get_thread_id(), Tag, __VA_ARGS__)
#define FATAL(Tag, ...)  ::Log::critical(__FILE__, __LINE__, ThreadPool::get_thread_id(), Tag, __VA_ARGS__)
