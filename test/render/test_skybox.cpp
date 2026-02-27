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
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/utils/path_utils.h"
#include "engine/core/log/Log.h"

/**
 * @file test/render/test_cosmic_skybox.cpp
 * @brief Rendering test for cosmic.jpg skybox
 */

DEFINE_LOG_TAG(LogCosmicSkybox, "CosmicSkybox");

static const std::string COSMIC_SCENE_PATH = "/Game/cosmic_skybox_scene.asset";

static bool create_cosmic_scene(const std::string& scene_path) {
    auto scene = std::make_shared<Scene>();
    
    // Camera - looking at different directions to see the skybox
    auto* cam_ent = scene->create_entity();
    auto* cam_trans = cam_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({0.0f, 0.0f, 0.0f});
    
    auto* cam = cam_ent->add_component<CameraComponent>();
    cam->set_fov(60.0f);
    
    // Create skybox entity
    auto* skybox_ent = scene->create_entity();
    auto* skybox_trans = skybox_ent->add_component<TransformComponent>();
    
    auto* skybox_comp = skybox_ent->add_component<SkyboxComponent>();
    
    // Load cosmic.jpg panorama texture
    auto panorama_texture = std::make_shared<Texture>("/Engine/textures/cosmic.jpg");
    if (!panorama_texture || !panorama_texture->texture_) {
        ERR(LogCosmicSkybox, "Failed to load cosmic.jpg");
        return false;
    }
    
    INFO(LogCosmicSkybox, "Loaded cosmic.jpg: {}x{}", 
         panorama_texture->texture_->mip_extent(0).width,
         panorama_texture->texture_->mip_extent(0).height);
    
    // Create skybox material
    auto material = std::make_shared<SkyboxMaterial>();
    material->set_intensity(1.0f);
    // Note: cube texture resolution is set in SkyboxComponent
    material->set_panorama_texture(panorama_texture);
    
    skybox_comp->set_material(material);
    skybox_comp->set_skybox_scale(1000.0f);
    
    INFO(LogCosmicSkybox, "Cosmic skybox scene created");
    
    EngineContext::asset()->save_asset(scene, scene_path);
    return true;
}

TEST_CASE("Cosmic Skybox Rendering", "[skybox]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = (utils::get_engine_path() / "test/test_internal").string();
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::render_system() != nullptr);
    REQUIRE(EngineContext::world() != nullptr);
    
    // Check if cosmic.jpg exists
    auto cosmic_path = EngineContext::asset()->get_physical_path("/Engine/textures/cosmic.jpg");
    if (!cosmic_path) {
        WARN(LogCosmicSkybox, "cosmic.jpg not found at /Engine/textures/cosmic.jpg, skipping test");
        test_utils::TestContext::reset();
        return;
    }
    
    test_utils::RenderTestApp::Config config;
    config.scene_path = COSMIC_SCENE_PATH;
    config.width = 1280;
    config.height = 720;
    config.max_frames = 60;
    config.capture_frame = 30;
    config.create_scene_func = create_cosmic_scene;
    
    std::vector<uint8_t> screenshot;
    int frames = 0;
    bool captured = test_utils::RenderTestApp::run(config, screenshot, &frames);
    
    CHECK(frames > 0);
    
    if (captured && !screenshot.empty()) {
        std::string path = test_asset_dir + "/cosmic_skybox_test.png";
        if (test_utils::save_screenshot_png(path, config.width, config.height, screenshot)) {
            float brightness = test_utils::calculate_average_brightness(screenshot);
            INFO(LogCosmicSkybox, "Cosmic skybox screenshot saved: {} (brightness: {:.1f})", path, brightness);
        }
    }
    
    test_utils::TestContext::reset();
}
