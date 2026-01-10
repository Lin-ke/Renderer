#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/asset/basic/png.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/component/spirit_component.h"
#include "engine/core/log/Log.h"
#include "engine/core/utils/file_cleaner.h"

TEST_CASE("Scene Dependency Integration Test", "[scene_asset]") {
    // Phase 1: Save Scene with Dependencies
    utils::clean_old_files(std::filesystem::path(std::string(ENGINE_PATH) + "/test/test_internal/assets"), 5);
    UID texture_uid, scene_uid;
    {
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        INFO(LogAsset, "--- Phase 1: Saving Scene ---");
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        // 1. Create a dependency asset (Texture)
        auto texture = std::make_shared<PNGAsset>();
        texture->width = 256;
        texture->height = 256;
        texture->channels = 4;
        texture->pixels.resize(256 * 256 * 4, 128); // Grey texture
        
        std::string texture_path = "/Game/texture.binasset";
        EngineContext::asset()->save_asset(texture, texture_path);
        texture_uid = texture->get_uid();
        INFO(LogAsset, "Texture UID: {}", texture_uid.to_string());

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
        INFO(LogAsset, "Scene UID: {}", scene_uid.to_string());

        EngineContext::exit();
    }

    // Phase 2: Load Scene and Verify
    {
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        INFO(LogAsset, "--- Phase 2: Loading Scene ---");
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
