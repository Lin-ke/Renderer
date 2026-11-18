#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include "engine/core/log/Log.h"
#include "test_utils.h"
#include <Windows.h>
#include <cstdlib>

int main(int argc, char* argv[]) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    // Initialize Engine once before all tests
    if (!test_utils::TestContext::init_engine()) return EXIT_FAILURE;
    Log::set_min_log_level(1);
    
    int result = Catch::Session().run(argc, argv);
    
    // Shutdown Engine once after all tests
    test_utils::TestContext::shutdown_engine();
    
    return result;
}
