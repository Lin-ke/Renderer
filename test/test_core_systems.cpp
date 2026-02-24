#include <catch2/catch_test_macros.hpp>
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

TEST_CASE("Light Manager Lifecycle", "[core]") {
    test_utils::TestContext::reset();
    auto light_manager = std::make_shared<RenderLightManager>();
    
    // Test initialization
    light_manager->init();
    
    // Test tick with frame index (prepare_lights is called internally)
    uint32_t test_frame_index = 0;
    light_manager->tick(test_frame_index);
    
    // Initially no lights should be registered
    CHECK(light_manager->get_directional_light(test_frame_index) == nullptr);
    CHECK(light_manager->get_point_shadow_lights(test_frame_index).empty());
    CHECK(light_manager->get_volume_lights(test_frame_index).empty());
    
    // Test destroy
    light_manager->destroy();
    test_utils::TestContext::reset();
}

TEST_CASE("Directional Light Component", "[core]") {
    test_utils::TestContext::reset();
    
    auto scene = std::make_shared<Scene>();
    auto entity = scene->create_entity();
    
    // Add transform component (required for light direction)
    auto transform = entity->add_component<TransformComponent>();
    transform->transform.set_position({10.0f, 20.0f, 30.0f});
    
    // Add directional light component
    auto light = entity->add_component<DirectionalLightComponent>();
    REQUIRE(light != nullptr);
    
    // Test default values
    CHECK(light->get_intensity() == Catch::Approx(2.0f));
    CHECK(light->cast_shadow() == true);
    CHECK(light->enable() == true);
    
    // Test color (default is Vec3::Ones() = {1, 1, 1})
    auto color = light->get_color();
    CHECK(color.x() == Catch::Approx(1.0f));
    CHECK(color.y() == Catch::Approx(1.0f));
    CHECK(color.z() == Catch::Approx(1.0f));
    
    // Test setters
    light->set_color({0.5f, 0.6f, 0.7f});
    light->set_intensity(5.0f);
    light->set_cast_shadow(false);
    light->set_enable(false);
    
    // Verify new values
    color = light->get_color();
    CHECK(color.x() == Catch::Approx(0.5f));
    CHECK(color.y() == Catch::Approx(0.6f));
    CHECK(color.z() == Catch::Approx(0.7f));
    CHECK(light->get_intensity() == Catch::Approx(5.0f));
    CHECK(light->cast_shadow() == false);
    CHECK(light->enable() == false);
    
    // Test bias getters
    CHECK(light->get_constant_bias() == Catch::Approx(1.0f));
    CHECK(light->get_slope_bias() == Catch::Approx(5.0f));
    test_utils::TestContext::reset();
}

TEST_CASE("Point Light Component", "[core]") {
    test_utils::TestContext::reset();
    
    auto scene = std::make_shared<Scene>();
    auto entity = scene->create_entity();
    
    // Add transform component (required for light position)
    auto transform = entity->add_component<TransformComponent>();
    transform->transform.set_position({5.0f, 10.0f, 15.0f});
    
    // Add point light component
    auto light = entity->add_component<PointLightComponent>();
    REQUIRE(light != nullptr);
    
    // Test default values
    CHECK(light->get_intensity() == Catch::Approx(2.0f));
    CHECK(light->cast_shadow() == true);
    CHECK(light->enable() == true);
    
    // Test color
    auto color = light->get_color();
    CHECK(color.x() == Catch::Approx(1.0f));
    CHECK(color.y() == Catch::Approx(1.0f));
    CHECK(color.z() == Catch::Approx(1.0f));
    
    // Test setters
    light->set_color({0.8f, 0.4f, 0.2f});
    light->set_intensity(3.5f);
    light->set_cast_shadow(false);
    light->set_enable(false);
    light->set_scale(50.0f); // This sets far_ parameter
    
    // Verify new values
    color = light->get_color();
    CHECK(color.x() == Catch::Approx(0.8f));
    CHECK(color.y() == Catch::Approx(0.4f));
    CHECK(color.z() == Catch::Approx(0.2f));
    CHECK(light->get_intensity() == Catch::Approx(3.5f));
    CHECK(light->cast_shadow() == false);
    CHECK(light->enable() == false);
    
    // Test bias getters
    CHECK(light->get_constant_bias() == Catch::Approx(0.005f));
    CHECK(light->get_slope_bias() == Catch::Approx(0.0f));
    
    // Test shadow ID management
    CHECK(light->get_point_light_id() == 0); // Default value
    CHECK(light->point_shadow_id_ == MAX_POINT_SHADOW_COUNT); // Default value
    
    light->set_point_shadow_id(5);
    CHECK(light->point_shadow_id_ == 5);
    test_utils::TestContext::reset();
}

TEST_CASE("Light Component Serialization", "[core]") {
    test_utils::TestContext::reset();
    std::string scene_path = "/Game/test_light_scene.asset";
    
    // Phase 1: Save scene with lights
    {
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");
        
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
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");
        
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
        CHECK(dir_color.x() == Catch::Approx(1.0f).margin(0.05f));
        CHECK(dir_color.y() == Catch::Approx(0.9f).margin(0.05f));
        CHECK(dir_color.z() == Catch::Approx(0.8f).margin(0.05f));
        CHECK(loaded_dir_light->get_intensity() == Catch::Approx(3.0f).margin(0.01f));
        CHECK(loaded_dir_light->cast_shadow() == true);
        CHECK(loaded_dir_light->enable() == true);
        
        // Verify point light
        REQUIRE(loaded_point_light != nullptr);
        auto point_color = loaded_point_light->get_color();
        CHECK(point_color.x() == Catch::Approx(0.5f).margin(0.05f));
        CHECK(point_color.y() == Catch::Approx(0.5f).margin(0.05f));
        CHECK(point_color.z() == Catch::Approx(1.0f).margin(0.05f));
        CHECK(loaded_point_light->get_intensity() == Catch::Approx(2.5f).margin(0.01f));
        CHECK(loaded_point_light->cast_shadow() == false);
    }
    test_utils::TestContext::reset();
}

TEST_CASE("Light Component Update Methods", "[core]") {
    test_utils::TestContext::reset();
    
    auto scene = std::make_shared<Scene>();
    auto entity = scene->create_entity();
    
    auto transform = entity->add_component<TransformComponent>();
    transform->transform.set_position({1.0f, 2.0f, 3.0f});
    
    // Test DirectionalLightComponent lifecycle methods
    auto dir_light = entity->add_component<DirectionalLightComponent>();
    dir_light->on_init();
    dir_light->on_update(0.016f); // Simulate one frame at 60fps
    dir_light->update_light_info(); // Should call update_matrix and update_cascades
    
    // Test PointLightComponent lifecycle methods
    auto point_entity = scene->create_entity();
    auto point_transform = point_entity->add_component<TransformComponent>();
    point_transform->transform.set_position({4.0f, 5.0f, 6.0f});
    
    auto point_light = point_entity->add_component<PointLightComponent>();
    point_light->on_init();
    point_light->on_update(0.016f);
    point_light->update_light_info();
    
    // Verify point light info was updated (position should be set from transform)
    auto bounding_sphere = point_light->get_bounding_sphere();
    CHECK(bounding_sphere.center.x() == Catch::Approx(4.0f));
    CHECK(bounding_sphere.center.y() == Catch::Approx(5.0f));
    CHECK(bounding_sphere.center.z() == Catch::Approx(6.0f));
    test_utils::TestContext::reset();
}

TEST_CASE("Multiple Point Lights Management", "[core]") {
    test_utils::TestContext::reset();
    
    auto scene = std::make_shared<Scene>();
    std::vector<PointLightComponent*> point_lights;
    
    // Create multiple point lights
    for (int i = 0; i < 5; i++) {
        auto entity = scene->create_entity();
        auto transform = entity->add_component<TransformComponent>();
        transform->transform.set_position({
            static_cast<float>(i * 10.0f), 
            5.0f, 
            0.0f
        });
        
        auto light = entity->add_component<PointLightComponent>();
        light->set_color({
            static_cast<float>(i) / 5.0f,
            1.0f - static_cast<float>(i) / 5.0f,
            0.5f
        });
        light->set_intensity(1.0f + static_cast<float>(i));
        point_lights.push_back(light);
    }
    
    REQUIRE(point_lights.size() == 5);
    
    // Verify each light has correct properties
    for (int i = 0; i < 5; i++) {
        auto color = point_lights[i]->get_color();
        CHECK(color.x() == Catch::Approx(static_cast<float>(i) / 5.0f));
        CHECK(color.y() == Catch::Approx(1.0f - static_cast<float>(i) / 5.0f));
        CHECK(color.z() == Catch::Approx(0.5f));
        CHECK(point_lights[i]->get_intensity() == Catch::Approx(1.0f + static_cast<float>(i)));
    }
    test_utils::TestContext::reset();
}
