#include "Log.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <chrono>
#include <string_view>
#include <mutex>
#include <ctime>
#include <atomic>
#include <sstream>
#include <iomanip>
#include "engine/core/utils/file_cleaner.h"


static std::string g_session_log_filename;        // 用于在测试时记住文件名，避免产生碎片文件
static std::ofstream g_log_file_stream;           // 自定义文件流
static std::mutex g_log_mutex;                    // 写锁
static bool g_is_glog_lib_initialized = false;
static CustomLogSink g_custom_log_sink;           // 自定义 Sink


void CustomLogSink::send(google::LogSeverity severity, const char* full_filename,
                         const char* base_filename, int line,
                         const struct tm* tm_time,
                         const char* message, size_t message_len) {
    std::lock_guard<std::mutex> lock(g_log_mutex);

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
    localtime_s(&local_tm, &t);

    
    std::ostringstream time_oss;
    time_oss << "[" << std::setw(2) << std::setfill('0') << local_tm.tm_hour << "-"
             << std::setw(2) << std::setfill('0') << local_tm.tm_min << "-"
             << std::setw(2) << std::setfill('0') << local_tm.tm_sec << "-"
             << std::setw(3) << std::setfill('0') << ms.count() << "]";
    std::string time_str = time_oss.str();

    std::string_view filename_view(full_filename);
    size_t last_slash = filename_view.find_last_of("/\\");
    if (last_slash != std::string_view::npos) {
        filename_view = filename_view.substr(last_slash + 1);
    }
    
    
    std::string clean_message;
    for (size_t i = 0; i < message_len; i++) {
        unsigned char c = static_cast<unsigned char>(message[i]);
        if (c >= 32 && c < 127) {
            clean_message += static_cast<char>(c);
        }
        // Skip non-ASCII characters
    }
    if (clean_message.empty() && message_len > 0) {
        clean_message = "<unicode>";
    }
    
    std::ostringstream msg_oss;
    msg_oss << time_str << " [" << filename_view << ":" << line << "] " << clean_message;
    std::string formatted_message = msg_oss.str();

    if (g_log_file_stream.is_open()) {
        g_log_file_stream << formatted_message << std::endl;
    }
}


std::atomic<bool> Log::initialized_{false};
std::mutex Log::init_mutex_;


void Log::init() {
    // Double-checked locking
    if (initialized_.load()) {
        return;
    }
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (initialized_.load()) { 
        return;
    }

    
    std::filesystem::path log_dir = std::filesystem::absolute(ENGINE_PATH) / ("logs");
    if (!std::filesystem::exists(log_dir)) {
        std::filesystem::create_directories(log_dir);
    }
    
    // 2. 决定文件名 & 清理旧文件
    // 如果 g_session_log_filename 为空，说明是本次进程第一次 init
    if (g_session_log_filename.empty()) {
        // 仅在第一次运行时清理，避免测试过程中误删
        utils::clean_old_files(log_dir.string(), 5); 

        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm;
        localtime_s(&local_tm, &t);

        std::ostringstream time_oss;
        time_oss << std::setw(4) << std::setfill('0') << (local_tm.tm_year + 1900) << "-"
                 << std::setw(2) << std::setfill('0') << (local_tm.tm_mon + 1) << "-"
                 << std::setw(2) << std::setfill('0') << local_tm.tm_mday << "-"
                 << std::setw(2) << std::setfill('0') << local_tm.tm_hour << "-"
                 << std::setw(2) << std::setfill('0') << local_tm.tm_min << "-"
                 << std::setw(2) << std::setfill('0') << local_tm.tm_sec;
        std::string time_str = time_oss.str();
        
        g_session_log_filename = (log_dir / ("renderer_" + time_str + ".log")).string();
    }

    g_log_file_stream.open(g_session_log_filename, std::ios::app);
    if (!g_is_glog_lib_initialized) {
        FLAGS_logtostderr = 1;
        FLAGS_alsologtostderr = 1;
        FLAGS_log_prefix = false;
        FLAGS_minloglevel = 0;

        
        google::SetLogDestination(google::GLOG_INFO, "");
        google::SetLogDestination(google::GLOG_WARNING, "");
        google::SetLogDestination(google::GLOG_ERROR, "");
        google::SetLogDestination(google::GLOG_FATAL, "");

        google::InitGoogleLogging("renderer");
        g_is_glog_lib_initialized = true;
    }
    
    
    google::AddLogSink(&g_custom_log_sink);
    
    
    LOG(INFO) << "Log system initialized";
    
    initialized_.store(true);
}

void Log::set_min_log_level(int level) {
    FLAGS_minloglevel = level;
}


void Log::shutdown() {
    if (!initialized_.load()) {
        return;
    }
    std::lock_guard<std::mutex> lock(init_mutex_);
    
    
    google::RemoveLogSink(&g_custom_log_sink);
    
    
    if (g_log_file_stream.is_open()) {
        g_log_file_stream.close();
    }
    
    
    
    initialized_.store(false);
}