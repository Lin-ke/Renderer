#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"
#include <filesystem>

/**
 * @file test_render_resource.cpp
 * @brief Unit tests for Texture and Material render resources.
 * All tests are merged into one to avoid lifecycle issues between tests.
 */

DEFINE_LOG_TAG(LogRenderResourceTest, "RenderResourceTest");

TEST_CASE("Render Resource Tests", "[render_resource]") {
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    
    // ========== Test 1: Texture RHI Initialization ==========
    {
        std::bitset<8> mode;
        mode.set(EngineContext::StartMode::Asset_);
        mode.set(EngineContext::StartMode::Window_);
        mode.set(EngineContext::StartMode::Render_);
        mode.set(EngineContext::StartMode::Single_Thread_);
        EngineContext::init(mode);
        EngineContext::asset()->init(test_asset_dir);

        INFO(LogRenderResourceTest, "Checking RHI backend...");
        REQUIRE(EngineContext::rhi() != nullptr);

        Extent3D extent = { 128, 128, 1 };
        auto texture = std::make_shared<Texture>(TextureType::Texture2D, FORMAT_R8G8B8A8_SRGB, extent);
        
        REQUIRE(texture->texture_ != nullptr);
        REQUIRE(texture->texture_view_ != nullptr);
        REQUIRE(texture->get_texture_type() == TextureType::Texture2D);
        
        std::vector<uint32_t> dummy_data(128 * 128, 0xFF0000FF);
        texture->set_data(dummy_data.data(), static_cast<uint32_t>(dummy_data.size() * sizeof(uint32_t)));

        // Explicitly destroy texture before exit
        texture.reset();
        
        EngineContext::exit();
    }

    // ========== Test 2: Material Parameters and Serialization ==========
    {
        std::bitset<8> mode;
        mode.set(EngineContext::StartMode::Asset_);
        mode.set(EngineContext::StartMode::Window_);
        mode.set(EngineContext::StartMode::Render_);
        mode.set(EngineContext::StartMode::Single_Thread_);
        EngineContext::init(mode);
        EngineContext::asset()->init(test_asset_dir);

        auto material = std::make_shared<Material>();
        
        Vec4 diffuse_color = { 1.0f, 0.5f, 0.2f, 1.0f };
        material->set_diffuse(diffuse_color);
        material->set_roughness(0.75f);
        material->set_metallic(0.1f);
        
        REQUIRE(material->get_diffuse().x() == Catch::Approx(1.0f));
        REQUIRE(material->get_roughness() == Catch::Approx(0.75f));
        
        std::string material_path = "/Game/test_material.asset";
        EngineContext::asset()->save_asset(material, material_path);
        
        auto loaded_material = EngineContext::asset()->load_asset<Material>(material_path);
        REQUIRE(loaded_material != nullptr);
        REQUIRE(loaded_material->get_diffuse().y() == Catch::Approx(0.5f));
        REQUIRE(loaded_material->get_metallic() == Catch::Approx(0.1f));

        // Destroy materials before exit
        loaded_material.reset();
        material.reset();
        
        EngineContext::exit();
    }

    // ========== Test 3: Material Texture Dependencies ==========
    {
        std::bitset<8> mode;
        mode.set(EngineContext::StartMode::Asset_);
        mode.set(EngineContext::StartMode::Window_);
        mode.set(EngineContext::StartMode::Render_);
        mode.set(EngineContext::StartMode::Single_Thread_);
        EngineContext::init(mode);
        EngineContext::asset()->init(test_asset_dir);

        Extent3D extent = { 64, 64, 1 };
        auto texture = std::make_shared<Texture>(TextureType::Texture2D, FORMAT_R8G8B8A8_SRGB, extent);
        texture->texture_id_ = 123; 
        
        std::string texture_path = "/Game/test_tex.binasset";
        EngineContext::asset()->save_asset(texture, texture_path);
        UID tex_uid = texture->get_uid();

        auto material = std::make_shared<Material>();
        material->set_diffuse_texture(texture);
        material->set_texture_2d(texture, 3); 
        
        std::string material_path = "/Game/dep_material.asset";
        EngineContext::asset()->save_asset(material, material_path);

        // Destroy material and texture before exit
        material.reset();
        texture.reset();
        
        EngineContext::exit();
        
        // Cold reload
        EngineContext::init(mode);
        EngineContext::asset()->init(test_asset_dir);

        auto loaded_material = EngineContext::asset()->load_asset<Material>(material_path);
        REQUIRE(loaded_material != nullptr);
        
        REQUIRE(loaded_material->get_diffuse_texture() != nullptr);
        CHECK(loaded_material->get_diffuse_texture()->get_uid() == tex_uid);
        
        REQUIRE(loaded_material->get_texture_2d_list().size() >= 4);
        REQUIRE(loaded_material->get_texture_2d_list()[3] != nullptr);
        CHECK(loaded_material->get_texture_2d_list()[3]->get_uid() == tex_uid);

        // Destroy loaded material before exit
        loaded_material.reset();
        
        EngineContext::exit();
    }
}
