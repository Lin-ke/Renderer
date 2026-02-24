#define CATCH_CONFIG_RUNNER
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>
#include "engine/core/log/Log.h"
#include "test_utils.h"

int main(int argc, char* argv[]) {
    // Set log level to WARNING to reduce noise during tests
    // 0=INFO, 1=WARNING, 2=ERROR, 3=FATAL
    Log::set_min_log_level(1);
    
    // Initialize Engine once before all tests
    test_utils::TestContext::init_engine();
    
    int result = Catch::Session().run(argc, argv);
    
    // Shutdown Engine once after all tests
    test_utils::TestContext::shutdown_engine();
    
    return result;
}
