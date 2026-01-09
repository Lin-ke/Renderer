#include "Log.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <chrono>
#include <string_view>
#include <mutex>
#include <ctime>

namespace fs = std::filesystem;

static std::ofstream g_log_file_stream;
static std::mutex g_log_mutex;

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

    // Format: [h-m-s-ms] as requested (e.g. 19-04-05-123)
    // Using local_tm avoids UTC issues and duplicate fractional seconds.
    std::string time_str = std::format("[{:02}-{:02}-{:02}-{:03}]", 
        local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec, ms.count());

    std::string_view filename_view(full_filename);
    size_t last_slash = filename_view.find_last_of("/\\");
    if (last_slash != std::string_view::npos) {
        filename_view = filename_view.substr(last_slash + 1);
    }
    
    // Reconstruct the full message with the desired format.
    std::string formatted_message = std::format("{} [{}:{}] {}",
        time_str,
        filename_view,
        line,
        std::string_view(message, message_len)
    );

    // Output to both console and our custom file
    std::cout << formatted_message << std::endl;
    if (g_log_file_stream.is_open()) {
        g_log_file_stream << formatted_message << std::endl;
    }
}

static void clean_old_logs(const std::string& log_dir, size_t keep_count, const std::string& log_prefix = "") {
    if (!fs::exists(log_dir)) return;

    std::vector<fs::path> log_files;

    try {
        for (const auto& entry : fs::directory_iterator(log_dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                
                bool matches_prefix = log_prefix.empty() || filename.find(log_prefix) != std::string::npos;
                // Match our new log file name as well
                bool is_log_file = filename.find(".log") != std::string::npos || 
                                   filename.find(".INFO") != std::string::npos ||
                                   filename.find("INFO.") != std::string::npos; 

                if (matches_prefix && is_log_file) {
                    log_files.push_back(entry.path());
                }
            }
        }

        if (log_files.size() <= keep_count) return;

        std::sort(log_files.begin(), log_files.end(), [](const fs::path& a, const fs::path& b) {
            return fs::last_write_time(a) > fs::last_write_time(b);
        });

        std::cout << "[LogCleaner] Found " << log_files.size() << " logs. Cleaning up old files..." << std::endl;
        for (size_t i = keep_count; i < log_files.size(); ++i) {
            fs::remove(log_files[i]);
        }

    } catch (const std::exception& e) {
        std::cerr << "[LogCleaner] Error: " << e.what() << std::endl;
    }
}

static CustomLogSink g_custom_log_sink;

void Log::init() {
    // Check and create logs directory
    std::filesystem::path log_dir = std::filesystem::absolute(ENGINE_PATH) / ("logs");
    if (!std::filesystem::exists(log_dir)) {
        std::filesystem::create_directories(log_dir);
    }
    
    // Clean old logs, now targeting renderer*.log
    clean_old_logs(log_dir.generic_string(), 10, "renderer");

    // Open our custom log file
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
    localtime_s(&local_tm, &t);

    // Ensure filename has clear date-time: renderer_YYYY-MM-DD-HH-MM-SS.log
    std::string time_str = std::format("{:04}-{:02}-{:02}-{:02}-{:02}-{:02}", 
        local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday, 
        local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
    fs::path log_file_path = log_dir / std::format("renderer_{}.log", time_str);
    g_log_file_stream.open(log_file_path, std::ios::app);

    // Disable glog's default file and console logging mechanisms that use prefixes.
    // We are taking over with our sink.
    FLAGS_logtostderr = 0;
    FLAGS_alsologtostderr = 0;
    FLAGS_log_prefix = false; // This is critical

    google::InitGoogleLogging("renderer");
    
    // We don't set FLAGS_log_dir because our sink handles file logging.
    // This prevents glog from creating its own empty INFO/WARNING/ERROR files.

    google::AddLogSink(&g_custom_log_sink);
}

void Log::shutdown() {
    google::RemoveLogSink(&g_custom_log_sink);
    if (g_log_file_stream.is_open()) {
        g_log_file_stream.close();
    }
    google::ShutdownGoogleLogging();
}