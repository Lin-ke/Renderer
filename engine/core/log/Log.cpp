#include "Log.h"
#include <iostream>
#include <filesystem>

void Log::init() {
    std::cout << "Current working directory: " << std::filesystem::current_path() << std::endl;

    FLAGS_alsologtostderr = 1;
    google::InitGoogleLogging("renderer");
    
    // Set log destination for different severities if needed, or rely on defaults.
    // glog by default writes to /tmp or similar on unix, on windows maybe temp.
    // We can set log dir.
    FLAGS_log_dir = ".";
    // We can also set specific file destination
    // google::SetLogDestination(google::GLOG_INFO, "renderer_");
}
