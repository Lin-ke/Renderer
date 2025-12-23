#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <memory>

namespace Engine {
    class Log {
    public:
        static void init();
        
        static std::shared_ptr<spdlog::logger>& get_core_logger() { return s_core_logger; }

    private:
        static std::shared_ptr<spdlog::logger> s_core_logger;
    };
}

// Log macros
#define LOG(...)    ::Engine::Log::get_core_logger()->info(__VA_ARGS__)
#define WARN(...)   ::Engine::Log::get_core_logger()->warn(__VA_ARGS__)
#define ERR(...)  ::Engine::Log::get_core_logger()->error(__VA_ARGS__)
#define FATAL(...)  ::Engine::Log::get_core_logger()->critical(__VA_ARGS__)
