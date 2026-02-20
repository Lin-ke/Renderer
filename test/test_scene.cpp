#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/asset/basic/png.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/spirit_component.h"
#include "engine/core/log/Log.h"
#include "engine/core/utils/math_print.h"
#include "engine/core/utils/file_cleaner.h"

/**
 * @file test_scene.cpp
 * @brief Unit tests for Scene management including serialization and dependencies.
 */

TEST_CASE("Scene Serialization via AssetManager", "[scene]") {
    {
        EngineContext::init(1 << EngineContext::StartMode::Asset);
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        auto scene = std::make_shared<Scene>();
        auto entity = scene->create_entity();
        
        auto transform_comp = entity->add_component<TransformComponent>();
        transform_comp->transform.set_position({10.0f, 20.0f, 30.0f});
        transform_comp->transform.set_scale({2.0f, 2.0f, 2.0f});

        std::string scene_path = "/Game/test_scene.asset";
        EngineContext::asset()->save_asset(scene, scene_path);
        
        EngineContext::exit();
    }

    {
        EngineContext::init(1 << EngineContext::StartMode::Asset);
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

TEST_CASE("Scene Dependency Integration", "[scene]") {
    utils::clean_old_files(std::filesystem::path(std::string(ENGINE_PATH) + "/test/test_internal/assets"), 5);
    UID texture_uid, scene_uid;
    
    // Phase 1: Save Scene with Dependencies
    {
        EngineContext::init(1 << EngineContext::StartMode::Asset);
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        // 1. Create a dependency asset (Texture)
        auto texture = std::make_shared<PNGAsset>();
        texture->width = 256;
        texture->height = 256;
        texture->channels = 4;
        texture->pixels.resize(256 * 256 * 4, 128);
        
        std::string texture_path = "/Game/texture.binasset";
        EngineContext::asset()->save_asset(texture, texture_path);
        texture_uid = texture->get_uid();

        // 2. Create Scene
        auto scene = std::make_shared<Scene>();
        
        // 3. Create Entity and Component
        auto* entity = scene->create_entity();
        auto* spirit = entity->add_component<SpiritComponent>();
        
        // 4. Link Dependency
        spirit->texture = texture;

        // 5. Save Scene
        std::string scene_path = "/Game/level1.asset";
        EngineContext::asset()->save_asset(scene, scene_path);
        scene_uid = scene->get_uid();

        EngineContext::exit();
    }

    // Phase 2: Load Scene and Verify
    {
        EngineContext::init(1 << EngineContext::StartMode::Asset);
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        std::string scene_path = "/Game/level1.asset";
        auto loaded_scene = EngineContext::asset()->load_asset<Scene>(scene_path);

        REQUIRE(loaded_scene != nullptr);
        CHECK(loaded_scene->get_uid() == scene_uid);
        
        // Check Entity
        REQUIRE(loaded_scene->entities_.size() == 1);
        auto* entity = loaded_scene->entities_[0].get();
        REQUIRE(entity != nullptr);

        // Check Component
        auto* spirit = entity->get_component<SpiritComponent>();
        REQUIRE(spirit != nullptr);

        // Check Dependency
        REQUIRE(spirit->texture != nullptr);
        CHECK(spirit->texture->get_uid() == texture_uid);
        CHECK(spirit->texture->width == 256);

        EngineContext::exit();
    }
}
