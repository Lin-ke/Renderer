#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_resource/skybox_material.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"

/**
 * @file test/render_resource/test_skybox.cpp
 * @brief Unit tests for skybox material and related resources.
 */

DEFINE_LOG_TAG(LogSkyboxTest, "SkyboxTest");

TEST_CASE("SkyboxMaterial Creation and Parameters", "[render_resource][skybox]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);

    auto material = std::make_shared<SkyboxMaterial>();
    
    // Check default values
    REQUIRE(material->get_material_type() == MaterialType::Skybox);
    REQUIRE(material->get_intensity() == Catch::Approx(1.0f));
    REQUIRE(material->get_cube_texture() == nullptr);
    
    // Check pipeline states specific to skybox
    REQUIRE(material->depth_write() == false);  // Skybox doesn't write depth
    REQUIRE(material->cull_mode() == CULL_MODE_NONE);  // No culling for inside view
    REQUIRE(material->render_queue() == 10000);  // Render last
    REQUIRE(material->cast_shadow() == false);
    REQUIRE(material->use_for_depth_pass() == false);
    
    // Test intensity setting
    material->set_intensity(2.5f);
    REQUIRE(material->get_intensity() == Catch::Approx(2.5f));
    
    material->set_intensity(0.5f);
    REQUIRE(material->get_intensity() == Catch::Approx(0.5f));

    test_utils::TestContext::reset();
}

TEST_CASE("SkyboxMaterial Cube Texture", "[render_resource][skybox]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);

    // Create a cube texture
    Extent3D extent = { 512, 512, 1 };
    auto cube_texture = std::make_shared<Texture>(TextureType::TextureCube, FORMAT_R8G8B8A8_SRGB, extent, 6);
    REQUIRE(cube_texture->get_texture_type() == TextureType::TextureCube);
    
    std::string texture_path = "/Game/test_cube_texture.binasset";
    EngineContext::asset()->save_asset(cube_texture, texture_path);
    UID texture_uid = cube_texture->get_uid();

    // Create material and set cube texture
    auto material = std::make_shared<SkyboxMaterial>();
    material->set_cube_texture(cube_texture);
    
    REQUIRE(material->get_cube_texture() != nullptr);
    REQUIRE(material->get_cube_texture()->get_uid() == texture_uid);

    // Test setting invalid texture type (should fail gracefully)
    auto tex_2d = std::make_shared<Texture>(TextureType::Texture2D, FORMAT_R8G8B8A8_SRGB, extent);
    material->set_cube_texture(tex_2d);  // Should log error and not update
    
    // Texture should remain unchanged
    REQUIRE(material->get_cube_texture()->get_uid() == texture_uid);

    test_utils::TestContext::reset();
}

TEST_CASE("SkyboxMaterial Serialization", "[render_resource][skybox]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);

    // Create cube texture
    Extent3D extent = { 256, 256, 1 };
    auto cube_texture = std::make_shared<Texture>(TextureType::TextureCube, FORMAT_R8G8B8A8_SRGB, extent, 6);
    std::string texture_path = "/Game/skybox_cube.binasset";
    EngineContext::asset()->save_asset(cube_texture, texture_path);
    UID texture_uid = cube_texture->get_uid();

    // Create and configure material
    auto material = std::make_shared<SkyboxMaterial>();
    material->set_intensity(1.5f);
    material->set_cube_texture(cube_texture);
    
    std::string material_path = "/Game/test_skybox_material.asset";
    EngineContext::asset()->save_asset(material, material_path);

    // Clear and reload
    material.reset();
    cube_texture.reset();
    
    test_utils::TestContext::reset();
    EngineContext::asset()->init(test_asset_dir);

    // Load as base Material, then cast
    auto loaded_asset = EngineContext::asset()->load_asset<Material>(material_path);
    REQUIRE(loaded_asset != nullptr);
    REQUIRE(loaded_asset->get_material_type() == MaterialType::Skybox);
    
    auto loaded_skybox = std::dynamic_pointer_cast<SkyboxMaterial>(loaded_asset);
    REQUIRE(loaded_skybox != nullptr);
    REQUIRE(loaded_skybox->get_intensity() == Catch::Approx(1.5f));
    REQUIRE(loaded_skybox->get_cube_texture() != nullptr);
    REQUIRE(loaded_skybox->get_cube_texture()->get_uid() == texture_uid);

    test_utils::TestContext::reset();
}

TEST_CASE("SkyboxMaterial Shaders", "[render_resource][skybox]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);

    auto material = std::make_shared<SkyboxMaterial>();
    
    // Initially no shaders
    REQUIRE(material->get_vertex_shader() == nullptr);
    REQUIRE(material->get_fragment_shader() == nullptr);

    // Create and set shaders
    auto vs = std::make_shared<Shader>("/Engine/shaders/skybox_vs.cso", SHADER_FREQUENCY_VERTEX, "VSMain");
    auto ps = std::make_shared<Shader>("/Engine/shaders/skybox_ps.cso", SHADER_FREQUENCY_FRAGMENT, "PSMain");
    
    material->set_vertex_shader(vs);
    material->set_fragment_shader(ps);
    
    REQUIRE(material->get_vertex_shader() == vs);
    REQUIRE(material->get_fragment_shader() == ps);

    // Test serialization with shaders
    std::string material_path = "/Game/skybox_with_shaders.asset";
    EngineContext::asset()->save_asset(material, material_path);
    
    material.reset();
    vs.reset();
    ps.reset();
    
    test_utils::TestContext::reset();
    EngineContext::asset()->init(test_asset_dir);
    
    auto loaded = EngineContext::asset()->load_asset<SkyboxMaterial>(material_path);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->get_vertex_shader() != nullptr);
    REQUIRE(loaded->get_fragment_shader() != nullptr);

    test_utils::TestContext::reset();
}
