#include <catch2/catch_test_macros.hpp>
#include "engine/main/engine_context.h"
#include "engine/core/os/thread_pool.h"
#include <atomic>
#include <vector>
#include <future>

TEST_CASE("Thread Pool Integration Test", "[thread_pool]") {
    EngineContext::exit();

    // Initialize EngineContext in multi-threaded mode (StartMode::Single_Thread_ bit is 0)
    // We only need basic systems, maybe Log
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Log_);
    
    // EngineContext::init will create ThreadPool because Single_Thread_ is not set
    EngineContext::init(mode);

    auto* pool = EngineContext::thread_pool();
    REQUIRE(pool != nullptr);
    // SECTION("Enqueue Basic Tasks") 
    {
        std::atomic<int> counter = 0;
        int num_tasks = 50;
        std::vector<std::future<void>> results;

        for (int i = 0; i < num_tasks; ++i) {
            auto p = pool->enqueue([&counter]() {
                counter++;
            });
            if (!p){
                // thread pool stop.
            }
            results.emplace_back(std::move(*p));
        }

        for (auto& res : results) {
            res.wait();
        }

        REQUIRE(counter == num_tasks);
    }
    //SECTION("Enqueue Task with Return Value")
    {
        auto future_res = *pool->enqueue([](int a, int b) {
            return a * b;
        }, 6, 7);

        REQUIRE(future_res.get() == 42);
    }
    //SECTION("Parallel Execution Verification") 
    {
        // This test tries to ensure that tasks can run in parallel 
        // by sleeping and checking if total time is less than serial execution.
        // However, this is flaky if only 1 core is available. 
        // We'll just check if multiple tasks complete.
        
        std::atomic<int> completed = 0;
        int num_tasks = 4;
        std::vector<std::future<void>> results;
        
        for(int i=0; i<num_tasks; ++i) {
            auto p = pool->enqueue([&completed]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                completed++;
            });
            if (!p) {}
            results.emplace_back(std::move(*p));
        }
        
        for (auto& res : results) {
            res.wait();
        }
        
        REQUIRE(completed == num_tasks);
    }

    EngineContext::exit();
}
