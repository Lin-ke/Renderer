#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/core/log/Log.h"
#include "engine/core/utils/math_print.h"    

TEST_CASE("Scene Serialization Test via AssetManager", "[scene]") {
    {
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        INFO(LogAsset, "--- Phase 1: Saving Scene ---");
        
        // Use the internal test directory
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        auto scene = std::make_shared<Scene>();
        auto entity = scene->create_entity();
        
        auto transform_comp = entity->add_component<TransformComponent>();
        transform_comp->transform.set_position({10.0f, 20.0f, 30.0f});
        transform_comp->transform.set_scale({2.0f, 2.0f, 2.0f});

        // Save as a text-based asset (JSON under the hood for .asset)
        std::string scene_path = "/Game/test_scene.asset";
        EngineContext::asset()->save_asset(scene, scene_path);
        
        EngineContext::exit();
    }

    {
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        INFO(LogAsset, "--- Phase 2: Loading Scene ---");
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        std::string scene_path = "/Game/test_scene.asset";
        auto loaded_scene = EngineContext::asset()->load_asset<Scene>(scene_path);

        REQUIRE(loaded_scene != nullptr);
        REQUIRE(loaded_scene->entities_.size() == 1);

        auto* entity = loaded_scene->entities_[0].get();
        auto transform_comp = entity->get_component<TransformComponent>();
        
        REQUIRE(transform_comp != nullptr);

        auto pos = transform_comp->transform.get_position();
        INFO(LogAsset, "Loaded Position:{}", cereal::to_json_string(pos));
        CHECK(pos.x() == (10.0f));
        CHECK(pos.y() == (20.0f));
        CHECK(pos.z() == (30.0f));

        auto scale = transform_comp->transform.get_scale();
        CHECK(scale.x() == (2.0f));
        CHECK(scale.y() == (2.0f));
        CHECK(scale.z() == (2.0f));

        EngineContext::exit();
    }
}