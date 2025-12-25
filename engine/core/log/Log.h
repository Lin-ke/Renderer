#pragma once

#include <format>
#include <boost/log/trivial.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>

class Log {
public:
    static void init();

    template<typename... Args>
    static void info(std::format_string<Args...> fmt, Args&&... args) {
        BOOST_LOG_TRIVIAL(info) << std::format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void warn(std::format_string<Args...> fmt, Args&&... args) {
        BOOST_LOG_TRIVIAL(warning) << std::format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void error(std::format_string<Args...> fmt, Args&&... args) {
        BOOST_LOG_TRIVIAL(error) << std::format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void critical(std::format_string<Args...> fmt, Args&&... args) {
        BOOST_LOG_TRIVIAL(fatal) << std::format(fmt, std::forward<Args>(args)...);
    }
};

// Log macros adapted for the new Log class
#define INFO(...)    ::Log::info(__VA_ARGS__)
#define WARN(...)   ::Log::warn(__VA_ARGS__)
#define ERR(...)    ::Log::error(__VA_ARGS__)
#define FATAL(...)  ::Log::critical(__VA_ARGS__)
