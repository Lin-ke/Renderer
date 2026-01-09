#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/asset/basic/png.h"
#include "engine/core/log/Log.h"

TEST_CASE("Asset Manager Integration Test", "[asset]") {
    // Phase 1: Save Assets
    Log::init();
    UID rec1, rec2;
    {
        INFO(LogAsset, "--- Phase 1: Saving Assets ---");
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        // Use a temporary test directory for assets
        EngineContext::asset()->init(std::string(ENGINE_PATH) 
        + "/test/test_internal");


        // Create Binary Asset (Data)
        auto bin_asset = std::make_shared<PNGAsset>();
        bin_asset->width = 1024;
        bin_asset->height = 768;
        bin_asset->channels = 4;
        bin_asset->pixels.resize(1024 * 768 * 4, 255); // White texture
        
        INFO(LogAsset, "Binary Asset UID: {}", bin_asset->get_uid().to_string());
        std::string bin_path = "/Game/data.binasset";
        EngineContext::asset()->save_asset(bin_asset, bin_path);
        
        // Create JSON Asset (Meta) with dependency
        auto json_asset = std::make_shared<PNGAsset>();
        json_asset->width = 100;
        json_asset->height = 100;
        
        // Set dependency
        json_asset->dep2 = bin_asset;
        INFO(LogAsset, "JSON Asset UID: {}", json_asset->get_uid().to_string());

        rec1 = bin_asset->get_uid();
        rec2 = json_asset->get_uid();
        // Save JSON Asset
        std::string json_path = "/Game/meta.asset";
        EngineContext::asset()->save_asset(json_asset, json_path);

        EngineContext::exit();
    }

    // Phase 2: Load Assets (New Context)
    {
        INFO(LogAsset, "--- Phase 2: Loading Assets ---");
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        

        std::string json_path = "/Game/meta.asset";
        auto loaded_asset = EngineContext::asset()->load_asset<PNGAsset>(json_path);

        REQUIRE(loaded_asset != nullptr);
        CHECK(loaded_asset->width == 100);

        // Resolve dependency
        REQUIRE(loaded_asset->dep2 != nullptr);
        
        CHECK(loaded_asset->dep2->get_uid() == rec1);
        CHECK(loaded_asset->get_uid() == rec2);
        
        auto dep = loaded_asset->dep2;
        REQUIRE(dep != nullptr);
        CHECK(dep->width == 1024);

        EngineContext::exit();
    }
    
    // Phase 3: Recursive Save Test
    {
        INFO(LogAsset, "--- Phase 3: Recursive Save Test ---");
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        // Create new Parent and Child
        auto child = std::make_shared<PNGAsset>();
        child->width = 512;
        child->height = 512;
        // child is dirty by default

        auto parent = std::make_shared<PNGAsset>();
        parent->width = 10;
        parent->height = 10;
        parent->dep2 = child; // Set dependency

        std::string parent_path = "/Game/parent_recursive.asset";
        
        // Save parent ONLY. Child should be auto-saved.
        EngineContext::asset()->save_asset(parent, parent_path);

        EngineContext::exit();
    }

    // Phase 4: Verify Recursive Save
    {
        INFO(LogAsset, "--- Phase 4: Verify Recursive Save ---");
        EngineContext::init(1 << EngineContext::StartMode::Asset_);
        EngineContext::asset()->init(std::string(ENGINE_PATH) + "/test/test_internal");

        std::string parent_path = "/Game/parent_recursive.asset";
        auto loaded_parent = EngineContext::asset()->load_asset<PNGAsset>(parent_path);

        REQUIRE(loaded_parent != nullptr);
        CHECK(loaded_parent->width == 10);
        
        auto loaded_child = loaded_parent->dep2;
        REQUIRE(loaded_child != nullptr);
        CHECK(loaded_child->width == 512);

        EngineContext::exit();
    }

    Log::shutdown();
}