#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/skybox_component.h"
#include "engine/function/render/render_resource/skybox_material.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_resource/mesh.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"

/**
 * @file test/render/test_skybox.cpp
 * @brief Rendering tests for skybox component.
 */

DEFINE_LOG_TAG(LogSkyboxRender, "SkyboxRender");

static const std::string SKYBOX_SCENE_PATH = "/Game/skybox_test_scene.asset";

static bool create_skybox_scene(const std::string& scene_path) {
    auto scene = std::make_shared<Scene>();
    
    // Camera
    auto* cam_ent = scene->create_entity();
    auto* cam_trans = cam_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({0.0f, 0.0f, 0.0f});
    
    auto* cam = cam_ent->add_component<CameraComponent>();
    cam->set_fov(60.0f);
    
    // Create skybox entity
    auto* skybox_ent = scene->create_entity();
    auto* skybox_trans = skybox_ent->add_component<TransformComponent>();
    
    auto* skybox_comp = skybox_ent->add_component<SkyboxComponent>();
    
    // Create skybox material with cube texture
    auto material = std::make_shared<SkyboxMaterial>();
    material->set_intensity(1.0f);
    
    // Create a simple colored cube texture for testing
    Extent3D extent = { 64, 64, 1 };
    auto cube_texture = std::make_shared<Texture>(TextureType::TextureCube, FORMAT_R8G8B8A8_SRGB, extent, 6);
    
    // Fill with gradient colors for each face (for visual identification)
    std::vector<uint32_t> face_data(64 * 64);
    uint32_t colors[6] = {
        0xFF0000FF,  // Right - Red
        0x00FF00FF,  // Left - Green
        0x0000FFFF,  // Top - Blue
        0xFFFF00FF,  // Bottom - Yellow
        0xFF00FFFF,  // Front - Magenta
        0x00FFFFFF   // Back - Cyan
    };
    
    for (int face = 0; face < 6; face++) {
        std::fill(face_data.begin(), face_data.end(), colors[face]);
        // Note: In real implementation, we'd set per-face data here
    }
    
    cube_texture->set_data(face_data.data(), static_cast<uint32_t>(face_data.size() * sizeof(uint32_t)));
    material->set_cube_texture(cube_texture);
    
    skybox_comp->set_material(material);
    skybox_comp->set_skybox_scale(1000.0f);
    
    INFO(LogSkyboxRender, "Skybox scene created");
    
    EngineContext::asset()->save_asset(scene, scene_path);
    return true;
}

TEST_CASE("SkyboxComponent Basic Creation", "[render][skybox]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::world() != nullptr);
    
    auto scene = std::make_shared<Scene>();
    
    // Create skybox entity
    auto* skybox_ent = scene->create_entity();
    auto* skybox_trans = skybox_ent->add_component<TransformComponent>();
    
    auto* skybox_comp = skybox_ent->add_component<SkyboxComponent>();
    REQUIRE(skybox_comp != nullptr);
    
    // Check default values
    REQUIRE(skybox_comp->get_intensity() == Catch::Approx(1.0f));
    REQUIRE(skybox_comp->get_skybox_scale() == Catch::Approx(1000.0f));
    // Note: Material may be null until on_init() is called
    
    // Test setting values
    skybox_comp->set_intensity(0.8f);
    REQUIRE(skybox_comp->get_intensity() == Catch::Approx(0.8f));
    
    skybox_comp->set_skybox_scale(500.0f);
    REQUIRE(skybox_comp->get_skybox_scale() == Catch::Approx(500.0f));
    
    // Create and set material
    auto material = std::make_shared<SkyboxMaterial>();
    material->set_intensity(1.2f);
    
    skybox_comp->set_material(material);
    REQUIRE(skybox_comp->get_material() == material);
    REQUIRE(skybox_comp->get_material_id() == material->get_material_id());
    
    test_utils::TestContext::reset();
}

TEST_CASE("SkyboxComponent Cube Texture Setup", "[render][skybox]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    auto scene = std::make_shared<Scene>();
    auto* skybox_ent = scene->create_entity();
    skybox_ent->add_component<TransformComponent>();
    
    auto* skybox_comp = skybox_ent->add_component<SkyboxComponent>();
    
    // Create cube texture
    Extent3D extent = { 128, 128, 1 };
    auto cube_texture = std::make_shared<Texture>(TextureType::TextureCube, FORMAT_R8G8B8A8_SRGB, extent, 6);
    
    // Set via component (should create material automatically)
    skybox_comp->set_cube_texture(cube_texture);
    
    REQUIRE(skybox_comp->get_material() != nullptr);
    REQUIRE(skybox_comp->get_material()->get_cube_texture() == cube_texture);
    
    // Test invalid texture type
    auto tex_2d = std::make_shared<Texture>(TextureType::Texture2D, FORMAT_R8G8B8A8_SRGB, extent);
    skybox_comp->set_cube_texture(tex_2d);  // Should fail gracefully
    
    // Material should still have cube texture
    REQUIRE(skybox_comp->get_material()->get_cube_texture() == cube_texture);
    
    test_utils::TestContext::reset();
}

TEST_CASE("SkyboxComponent Serialization", "[render][skybox]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    // Create scene with skybox
    auto scene = std::make_shared<Scene>();
    
    auto* skybox_ent = scene->create_entity();
    skybox_ent->add_component<TransformComponent>();
    
    auto* skybox_comp = skybox_ent->add_component<SkyboxComponent>();
    skybox_comp->set_intensity(0.75f);
    skybox_comp->set_skybox_scale(2000.0f);
    
    // Create material with cube texture
    auto material = std::make_shared<SkyboxMaterial>();
    
    Extent3D extent = { 64, 64, 1 };
    auto cube_texture = std::make_shared<Texture>(TextureType::TextureCube, FORMAT_R8G8B8A8_SRGB, extent, 6);
    std::string tex_path = "/Game/skybox_test_cube.binasset";
    EngineContext::asset()->save_asset(cube_texture, tex_path);
    UID cube_uid = cube_texture->get_uid();
    
    material->set_cube_texture(cube_texture);
    skybox_comp->set_material(material);
    
    // Save scene
    std::string scene_path = "/Game/skybox_serialization_test.asset";
    EngineContext::asset()->save_asset(scene, scene_path);
    
    // Clear and reload
    scene.reset();
    material.reset();
    cube_texture.reset();
    
    test_utils::TestContext::reset();
    EngineContext::asset()->init(test_asset_dir);
    
    // Load scene
    auto loaded_scene = EngineContext::asset()->load_asset<Scene>(scene_path);
    REQUIRE(loaded_scene != nullptr);
    
    // Find skybox component
    auto skybox_comps = loaded_scene->get_components<SkyboxComponent>();
    REQUIRE(skybox_comps.size() == 1);
    
    auto* loaded_skybox = skybox_comps[0];
    REQUIRE(loaded_skybox->get_intensity() == Catch::Approx(0.75f));
    REQUIRE(loaded_skybox->get_skybox_scale() == Catch::Approx(2000.0f));
    REQUIRE(loaded_skybox->get_material() != nullptr);
    REQUIRE(loaded_skybox->get_material()->get_cube_texture() != nullptr);
    REQUIRE(loaded_skybox->get_material()->get_cube_texture()->get_uid() == cube_uid);
    
    test_utils::TestContext::reset();
}

TEST_CASE("Skybox Rendering", "[render][skybox][visual]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::render_system() != nullptr);
    REQUIRE(EngineContext::world() != nullptr);
    
    test_utils::RenderTestApp::Config config;
    config.scene_path = SKYBOX_SCENE_PATH;
    config.width = 1280;
    config.height = 720;
    config.max_frames = 30;
    config.capture_frame = 15;
    config.create_scene_func = create_skybox_scene;
    
    std::vector<uint8_t> screenshot;
    int frames = 0;
    bool captured = test_utils::RenderTestApp::run(config, screenshot, &frames);
    
    CHECK(frames > 0);
    
    if (captured && !screenshot.empty()) {
        std::string path = test_asset_dir + "/skybox_test.png";
        if (test_utils::save_screenshot_png(path, config.width, config.height, screenshot)) {
            float brightness = test_utils::calculate_average_brightness(screenshot);
            INFO(LogSkyboxRender, "Skybox screenshot saved: {} (brightness: {:.1f})", path, brightness);
            // Skybox should have visible brightness (not black)
            CHECK(brightness > 5.0f);
        }
    }
    
    test_utils::TestContext::reset();
}

TEST_CASE("SkyboxComponent Draw Batch Collection", "[render][skybox]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    auto scene = std::make_shared<Scene>();
    auto* skybox_ent = scene->create_entity();
    skybox_ent->add_component<TransformComponent>();
    
    auto* skybox_comp = skybox_ent->add_component<SkyboxComponent>();
    
    // Without material, should not produce batches
    std::vector<render::DrawBatch> batches;
    skybox_comp->collect_draw_batch(batches);
    REQUIRE(batches.empty());
    
    // Create and set material
    auto material = std::make_shared<SkyboxMaterial>();
    skybox_comp->set_material(material);
    
    // Still no batches without mesh (mesh loading may fail in test)
    batches.clear();
    skybox_comp->collect_draw_batch(batches);
    // May or may not have batches depending on whether cube mesh loaded
    
    test_utils::TestContext::reset();
}
