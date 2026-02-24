#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"

/**
 * @file test/render/test_basic.cpp
 * @brief Basic rendering tests.
 */

DEFINE_LOG_TAG(LogBasicRender, "BasicRender");

static const std::string BUNNY_SCENE_PATH = "/Game/bunny_scene.asset";
static const std::string BUNNY_MODEL_PATH = "/Engine/models/bunny.obj";

static bool create_bunny_scene(const std::string& scene_path) {
    auto scene = std::make_shared<Scene>();
    
    // Camera
    auto* cam_ent = scene->create_entity();
    auto* cam_trans = cam_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({-3.0f, 0.0f, 0.0f});
    
    auto* cam = cam_ent->add_component<CameraComponent>();
    cam->set_fov(60.0f);
    
    // Light
    auto* light_ent = scene->create_entity();
    auto* light_trans = light_ent->add_component<TransformComponent>();
    light_trans->transform.set_position({5.0f, 10.0f, 5.0f});
    light_trans->transform.set_rotation({0.0f, -45.0f, -60.0f});
    
    auto* light = light_ent->add_component<DirectionalLightComponent>();
    light->set_color({1.0f, 1.0f, 1.0f});
    light->set_intensity(1.5f);
    light->set_enable(true);
    
    // Bunny model
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    setting.load_materials = false;
    setting.material_type = ModelMaterialType::PBR;
    
    auto model = Model::Load(BUNNY_MODEL_PATH, setting);
    if (!model || model->get_submesh_count() == 0) {
        ERR(LogBasicRender, "Failed to load bunny model");
        return false;
    }
    
    auto* bunny_ent = scene->create_entity();
    auto* bunny_trans = bunny_ent->add_component<TransformComponent>();
    bunny_trans->transform.set_scale({10.0f, 10.0f, 10.0f});
    
    auto* bunny_mesh = bunny_ent->add_component<MeshRendererComponent>();
    bunny_mesh->set_model(model);
    
    // PBR material
    auto mat = std::make_shared<PBRMaterial>();
    mat->set_diffuse({0.8f, 0.5f, 0.3f, 1.0f});
    mat->set_roughness(0.2f);
    mat->set_metallic(0.8f);
    bunny_mesh->set_material(mat);
    
    INFO(LogBasicRender, "Bunny scene created with {} submeshes", model->get_submesh_count());
    
    EngineContext::asset()->save_asset(scene, scene_path);
    return true;
}

TEST_CASE("Bunny model rendering", "[render]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::render_system() != nullptr);
    REQUIRE(EngineContext::world() != nullptr);
    
    test_utils::RenderTestApp::Config config;
    config.scene_path = BUNNY_SCENE_PATH;
    config.width = 1280;
    config.height = 720;
    config.max_frames = 60;
    config.capture_frame = 30;
    config.create_scene_func = create_bunny_scene;
    
    std::vector<uint8_t> screenshot;
    int frames = 0;
    bool captured = test_utils::RenderTestApp::run(config, screenshot, &frames);
    
    CHECK(frames > 0);
    
    if (captured) {
        std::string path = test_asset_dir + "/bunny.png";
        if (test_utils::save_screenshot_png(path, config.width, config.height, screenshot)) {
            float brightness = test_utils::calculate_average_brightness(screenshot);
            INFO(LogBasicRender, "Screenshot saved: {} (brightness: {:.1f})", path, brightness);
            CHECK(brightness > 1.0f);
        }
    }
    
    test_utils::TestContext::reset();
}
