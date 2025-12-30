#include "Log.h"
#include <filesystem>
#include <iostream>
namespace fs = std::filesystem;
static void clean_old_logs(const std::string& log_dir, size_t keep_count, const std::string& log_prefix = "") {
    if (!fs::exists(log_dir)) return;

    std::vector<fs::path> log_files;

    try {
        for (const auto& entry : fs::directory_iterator(log_dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                
                bool matches_prefix = log_prefix.empty() || filename.find(log_prefix) != std::string::npos;
                bool is_log_file = filename.find(".log") != std::string::npos || 
                                   filename.find(".INFO") != std::string::npos ||
                                   filename.find("INFO.") != std::string::npos; // glog通常包含这些

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
            std::cout << "  Deleted: " << log_files[i].filename() << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "[LogCleaner] Error: " << e.what() << std::endl;
    }
}

void Log::init() {
    // Check and create logs directory
    std::filesystem::path log_dir = std::filesystem::absolute(ENGINE_PATH) / ("logs");
    if (!std::filesystem::exists(log_dir)) {
        std::filesystem::create_directories(log_dir);
    }
    // Clean old logs, keep last 10
    clean_old_logs(log_dir.generic_string(), 10);
    
    FLAGS_alsologtostderr = 1;
    google::InitGoogleLogging("renderer");
    FLAGS_log_dir = log_dir.generic_string();
}
