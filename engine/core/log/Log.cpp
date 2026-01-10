#include "Log.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <chrono>
#include <string_view>
#include <mutex>
#include <ctime>
#include <atomic>
#include "engine/core/utils/file_cleaner.h"

// --- 全局静态变量 ---
static std::string g_session_log_filename;        // 用于在测试时记住文件名，避免产生碎片文件
static std::ofstream g_log_file_stream;           // 自定义文件流
static std::mutex g_log_mutex;                    // 写锁
static bool g_is_glog_lib_initialized = false;    // 【关键修改】控制 Glog 库的初始化状态
static CustomLogSink g_custom_log_sink;           // 自定义 Sink

// --- Sink 实现 (保持不变) ---
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

    // Format: [h-m-s-ms] 
    std::string time_str = std::format("[{:02}-{:02}-{:02}-{:03}]", 
        local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec, ms.count());

    std::string_view filename_view(full_filename);
    size_t last_slash = filename_view.find_last_of("/\\");
    if (last_slash != std::string_view::npos) {
        filename_view = filename_view.substr(last_slash + 1);
    }
    
    std::string formatted_message = std::format("{} [{}:{}] {}",
        time_str,
        filename_view,
        line,
        std::string_view(message, message_len)
    );

    std::cout << formatted_message << std::endl;
    if (g_log_file_stream.is_open()) {
        g_log_file_stream << formatted_message << std::endl;
    }
}

// --- Log 类静态成员 ---
std::atomic<bool> Log::initialized_{false};
std::mutex Log::init_mutex_;

// --- Log::init 实现 ---
void Log::init() {
    // Double-checked locking 防止重复调用 Log::init
    if (initialized_.load()) {
        return;
    }
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (initialized_.load()) { 
        return;
    }

    // 1. 准备日志目录
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

        std::string time_str = std::format("{:04}-{:02}-{:02}-{:02}-{:02}-{:02}", 
            local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday, 
            local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
        
        g_session_log_filename = (log_dir / std::format("renderer_{}.log", time_str)).string();
    }

    g_log_file_stream.open(g_session_log_filename, std::ios::app);
    if (!g_is_glog_lib_initialized) {
        FLAGS_logtostderr = 0;
        FLAGS_alsologtostderr = 0;
        FLAGS_log_prefix = false;

        // 【关键】清空默认日志路径，禁止 Glog 创建默认的 .INFO/.ERROR 文件
        // 这解决了 "File exists" 和文件碎片问题
        google::SetLogDestination(google::GLOG_INFO, "");
        google::SetLogDestination(google::GLOG_WARNING, "");
        google::SetLogDestination(google::GLOG_ERROR, "");
        google::SetLogDestination(google::GLOG_FATAL, "");

        google::InitGoogleLogging("renderer");
        g_is_glog_lib_initialized = true;
    }
    
    // 5. 挂载自定义 Sink
    google::AddLogSink(&g_custom_log_sink);
    
    initialized_.store(true);
}

// --- Log::shutdown 实现 ---
void Log::shutdown() {
    if (!initialized_.load()) {
        return;
    }
    std::lock_guard<std::mutex> lock(init_mutex_);
    
    // 1. 移除 Sink，停止接收新日志
    google::RemoveLogSink(&g_custom_log_sink);
    
    // 2. 关闭文件流
    if (g_log_file_stream.is_open()) {
        g_log_file_stream.close();
    }
    
    // 【关键】不要调用 google::ShutdownGoogleLogging()
    // 让 Glog 库保持存活直到进程结束。
    // 这避免了在单元测试快速重启时，Glog 内部资源释放不及时导致的问题。
    // google::ShutdownGoogleLogging(); 
    
    initialized_.store(false);
}