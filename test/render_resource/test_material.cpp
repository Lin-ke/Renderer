#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/main/engine_context.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"

/**
 * @file test/render_resource/test_material.cpp
 * @brief Unit tests for Material render resource.
 */

DEFINE_LOG_TAG(LogMaterialTest, "MaterialTest");

TEST_CASE("Material Parameters and Serialization", "[render_resource]") {
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::SingleThread);
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

    loaded_material.reset();
    material.reset();
    
    EngineContext::exit();
}

TEST_CASE("Material Texture Dependencies", "[render_resource]") {
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::SingleThread);
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

    loaded_material.reset();
    
    EngineContext::exit();
}
