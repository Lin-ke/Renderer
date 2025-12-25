#include "Log.h"
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <iostream>
#include <filesystem>

namespace logging = boost::log;
namespace keywords = boost::log::keywords;
namespace expr = boost::log::expressions;

void Log::init() {
    std::cout << "Current working directory: " << std::filesystem::current_path() << std::endl;

    logging::add_common_attributes();

    // Console sink
    logging::add_console_log(
        std::cout,
        keywords::format = (
            expr::stream
            << "[" << expr::format_date_time< boost::posix_time::ptime >("TimeStamp", "%H:%M:%S")
            << "] [" << logging::trivial::severity
            << "] " << expr::smessage
        )
    );

    // File sink
    logging::add_file_log(
        keywords::file_name = "renderer.log",
        keywords::auto_flush = true,
        keywords::format = (
            expr::stream
            << "[" << expr::format_date_time< boost::posix_time::ptime >("TimeStamp", "%H:%M:%S")
            << "] [" << logging::trivial::severity
            << "] " << expr::smessage
        )
    );

    logging::core::get()->set_filter(
        logging::trivial::severity >= logging::trivial::trace
    );
}