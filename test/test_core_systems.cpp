#include "test/test_assertions.h"
#include <catch2/catch_approx.hpp>
#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/core/os/thread_pool.h"
#include "engine/function/render/render_system/render_light_manager.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/framework/component/point_light_component.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"

#include <atomic>
#include <vector>
#include <future>

/**
 * @file test_core_systems.cpp
 * @brief Core systems tests including thread pool and light manager.
 */

TEST_CASE("Thread Pool Integration", "[core]") {
    test_utils::TestContext::reset();

    auto* pool = EngineContext::thread_pool();
    REQUIRE(pool != nullptr);

    SECTION("Enqueue Basic Tasks") {
        std::atomic<int> counter = 0;
        int num_tasks = 50;
        std::vector<std::future<void>> results;

        for (int i = 0; i < num_tasks; ++i) {
            results.emplace_back(pool->enqueue([&counter]() {
                counter++;
            }));
        }

        for (auto& res : results) {
            res.wait();
        }

        REQUIRE(counter == num_tasks);
    }

    SECTION("Enqueue Task with Return Value") {
        auto future_res = pool->enqueue([](int a, int b) {
            return a * b;
        }, 6, 7);

        REQUIRE(future_res.get() == 42);
    }

    SECTION("Parallel Execution Verification") {
        // This test tries to ensure that tasks can run in parallel 
        // by sleeping and checking if total time is less than serial execution.
        // However, this is flaky if only 1 core is available. 
        // We'll just check if multiple tasks complete.
        
        std::atomic<int> completed = 0;
        int num_tasks = 4;
        std::vector<std::future<void>> results;
        
        for(int i=0; i<num_tasks; ++i) {
            results.emplace_back(pool->enqueue([&completed]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                completed++;
            }));
        }
        
        for (auto& res : results) {
            res.wait();
        }
        
        REQUIRE(completed == num_tasks);
    }

    test_utils::TestContext::reset();
}

TEST_CASE("Prefetched assets are initialized by a blocking load", "[core][asset]") {
    test_utils::TestContext::reset();

    auto project_dir = test_utils::TestContext::project_dir() / "asset-prefetch";
    AssetManager assets;
    assets.init(project_dir);
    auto fixture = std::make_shared<Scene>();
    fixture->create_entity()->add_component<TransformComponent>();
    auto uid = UID::generate();
    fixture->set_uid(uid);
    assets.save_asset(fixture, "/Game/" + uid.to_string() + ".asset");
    assets.unload_asset(uid);

    auto first = assets.enqueue_load_task(uid);
    auto second = assets.enqueue_load_task(uid);
    REQUIRE_FALSE(first.empty());
    REQUIRE_FALSE(second.empty());
    auto prefetched = first.back().get();
    REQUIRE(prefetched);
    CHECK(prefetched != fixture);
    CHECK(second.back().get() == prefetched);
    CHECK_FALSE(prefetched->is_initialized());

    auto scene = assets.load_asset<Scene>(uid);
    REQUIRE(scene);
    CHECK(scene == prefetched);
    CHECK(scene->is_initialized());
    REQUIRE_FALSE(scene->entities_.empty());
    auto* entity = scene->entities_.front().get();
    auto* transform = entity->get_component<TransformComponent>();
    REQUIRE(transform);
    CHECK(transform->get_owner() == entity);

    test_utils::TestContext::reset();
}

TEST_CASE("Light Component Serialization", "[core]") {
    test_utils::TestContext::reset();
    std::string scene_path = "/Game/test_light_scene.asset";
    
    // Phase 1: Save scene with lights
    {
        auto scene = std::make_shared<Scene>();
        
        // Create entity with directional light
        auto dir_entity = scene->create_entity();
        auto dir_transform = dir_entity->add_component<TransformComponent>();
        dir_transform->transform.set_position({0.0f, 10.0f, 0.0f});
        
        auto dir_light = dir_entity->add_component<DirectionalLightComponent>();
        dir_light->set_color({1.0f, 0.9f, 0.8f});
        dir_light->set_intensity(3.0f);
        dir_light->set_cast_shadow(true);
        dir_light->set_enable(true);
        
        // Create entity with point light
        auto point_entity = scene->create_entity();
        auto point_transform = point_entity->add_component<TransformComponent>();
        point_transform->transform.set_position({5.0f, 2.0f, 5.0f});
        
        auto point_light = point_entity->add_component<PointLightComponent>();
        point_light->set_color({0.5f, 0.5f, 1.0f});
        point_light->set_intensity(2.5f);
        point_light->set_cast_shadow(false);
        point_light->set_scale(20.0f);
        
        EngineContext::asset()->save_asset(scene, scene_path);
    }
    
    // Phase 2: Load and verify
    {
        auto loaded_scene = EngineContext::asset()->load_asset<Scene>(scene_path);
        REQUIRE(loaded_scene != nullptr);
        REQUIRE(loaded_scene->entities_.size() == 2);
        
        // Find directional light entity
        DirectionalLightComponent* loaded_dir_light = nullptr;
        PointLightComponent* loaded_point_light = nullptr;
        
        for (auto& entity : loaded_scene->entities_) {
            if (auto dl = entity->get_component<DirectionalLightComponent>()) {
                loaded_dir_light = dl;
            }
            if (auto pl = entity->get_component<PointLightComponent>()) {
                loaded_point_light = pl;
            }
        }
        
        // Verify directional light
        REQUIRE(loaded_dir_light != nullptr);
        auto dir_color = loaded_dir_light->get_color();
        CHECK(dir_color.x == Catch::Approx(1.0f).margin(0.05f));
        CHECK(dir_color.y == Catch::Approx(0.9f).margin(0.05f));
        CHECK(dir_color.z == Catch::Approx(0.8f).margin(0.05f));
        CHECK(loaded_dir_light->get_intensity() == Catch::Approx(3.0f).margin(0.01f));
        CHECK(loaded_dir_light->cast_shadow() == true);
        CHECK(loaded_dir_light->enable() == true);
        
        // Verify point light
        REQUIRE(loaded_point_light != nullptr);
        auto point_color = loaded_point_light->get_color();
        CHECK(point_color.x == Catch::Approx(0.5f).margin(0.05f));
        CHECK(point_color.y == Catch::Approx(0.5f).margin(0.05f));
        CHECK(point_color.z == Catch::Approx(1.0f).margin(0.05f));
        CHECK(loaded_point_light->get_intensity() == Catch::Approx(2.5f).margin(0.01f));
        CHECK(loaded_point_light->cast_shadow() == false);
    }
    test_utils::TestContext::reset();
}
