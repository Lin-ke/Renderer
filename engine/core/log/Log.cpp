#include "Log.h"
#include <filesystem>

void Log::init() {
    // Check and create logs directory
    std::filesystem::path log_dir = std::filesystem::path(ENGINE_PATH) / ("logs/");
    if (!std::filesystem::exists(log_dir)) {
        std::filesystem::create_directories(log_dir);
    }
    
    FLAGS_alsologtostderr = 1;
    google::InitGoogleLogging("renderer");
    FLAGS_log_dir = "./logs/";
}
