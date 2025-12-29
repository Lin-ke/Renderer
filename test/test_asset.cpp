#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/asset/basic/png.h"
#include "engine/core/log/Log.h"

// Register types
CEREAL_REGISTER_TYPE(PNGAsset);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, PNGAsset);

TEST_CASE("Asset Manager Integration Test", "[asset]") {
    // Phase 1: Save Assets
    {
        EngineContext::init();
        // Use a temporary test directory for assets
        EngineContext::asset()->init("test_game_assets");

        INFO("--- Phase 1: Saving Assets ---");

        // Create Binary Asset (Data)
        auto bin_asset = std::make_shared<PNGAsset>();
        bin_asset->set_uid(UID());
        bin_asset->width = 1024;
        bin_asset->height = 768;
        bin_asset->channels = 4;
        bin_asset->pixels.resize(1024 * 768 * 4, 255); // White texture
        
        // Save Binary Asset
        std::string bin_path = "texture/data.binasset";
        EngineContext::asset()->save_asset(bin_asset, bin_path);
        
        // Create JSON Asset (Meta) with dependency
        auto json_asset = std::make_shared<PNGAsset>();
        json_asset->set_uid(UID());
        json_asset->width = 100;
        json_asset->height = 100;
        
        // Set dependency
        json_asset->dep2 = AssetHandle<PNGAsset>(bin_asset);
        
        // Save JSON Asset
        std::string json_path = "texture/meta.asset";
        EngineContext::asset()->save_asset(json_asset, json_path);

        EngineContext::exit();
    }

    // Phase 2: Load Assets (New Context)
    {
        EngineContext::init();
        EngineContext::asset()->init("test_game_assets");

        INFO("--- Phase 2: Loading Assets ---");

        std::string json_path = "texture/meta.asset";
        auto loaded_asset = EngineContext::asset()->get_or_load_asset<PNGAsset>(json_path);

        REQUIRE(loaded_asset != nullptr);
        CHECK(loaded_asset->width == 100);

        // Resolve dependency
        loaded_asset->dep2.resolve();
        
        REQUIRE(loaded_asset->dep2.is_loaded());
        
        auto dep = loaded_asset->dep2.get();
        REQUIRE(dep != nullptr);
        CHECK(dep->width == 1024);

        EngineContext::exit();
    }
}