#define CATCH_CONFIG_RUNNER
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>
#include "engine/core/log/Log.h"

int main(int argc, char* argv[]) {
    // Set log level to WARNING to reduce noise during tests
    // 0=INFO, 1=WARNING, 2=ERROR, 3=FATAL
    Log::set_min_log_level(1);
    
    int result = Catch::Session().run(argc, argv);
    
    return result;
}
