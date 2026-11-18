#include "test/test_assertions.h"
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
    
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::render_system() != nullptr);
    REQUIRE(EngineContext::world() != nullptr);
    
    const auto cosmic_path = EngineContext::asset()->get_physical_path("/Engine/textures/cosmic.jpg");
    REQUIRE(cosmic_path);
    REQUIRE(std::filesystem::is_regular_file(*cosmic_path));

    test_utils::RenderTestApp::Config config;
    config.scene_path = COSMIC_SCENE_PATH;
    config.max_frames = 60;
    config.capture_frame = 30;
    config.create_scene_func = create_cosmic_scene;
    
    std::vector<uint8_t> screenshot;
    REQUIRE(test_utils::RenderTestApp::run(config, screenshot));
    CHECK(test_utils::calculate_average_brightness(screenshot) > 1.0f);

    test_utils::TestContext::reset();
}
