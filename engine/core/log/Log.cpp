#include "Log.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <Windows.h>
#include <iostream>

namespace Engine {

    std::shared_ptr<spdlog::logger> Log::core_logger_;

    void Log::init() {
        // Allocate a console window for output
        AllocConsole();
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);

        // Create sinks: Console and File
        std::vector<spdlog::sink_ptr> log_sinks;
        log_sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        log_sinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("renderer.log", true));

        // Format pattern: [Time] [Logger] [Level] Message
        log_sinks[0]->set_pattern("%^[%T] %n: %v%$");
        log_sinks[1]->set_pattern("[%T] [%l] %n: %v");

        core_logger_ = std::make_shared<spdlog::logger>("ENGINE", begin(log_sinks), end(log_sinks));
        spdlog::register_logger(core_logger_);
        core_logger_->set_level(spdlog::level::trace);
        core_logger_->flush_on(spdlog::level::trace);
    }
}
