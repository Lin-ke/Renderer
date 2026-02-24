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
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/input/input.h"
#include "engine/core/log/Log.h"

#include <thread>
#include <chrono>
#include <vector>

DEFINE_LOG_TAG(LogBunnyRender, "BunnyRender");

static const std::string SCENE_PATH = "/Game/bunny_scene.asset";
static const std::string MODEL_PATH = "/Engine/models/bunny.obj";
static const std::string MODEL_ASSET_PATH = "/Game/models/bunny.asset";

/**
 * @brief Part 1: Create and setup bunny test scene, save to file
 */
static bool create_and_save_bunny_scene(const std::string& scene_path) {
    INFO(LogBunnyRender, "=== Part 1: Creating Scene ===");
    
    auto scene = std::make_shared<Scene>();
    
    // Create camera entity
    auto* camera_ent = scene->create_entity();
    auto* cam_trans = camera_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({-3.0f, 0.0f, 0.0f});
    
    auto* camera = camera_ent->add_component<CameraComponent>();
    camera->set_fov(60.0f);
    
    // Create directional light entity
    auto* light_ent = scene->create_entity();
    auto* light_trans = light_ent->add_component<TransformComponent>();
    light_trans->transform.set_position({5.0f, 10.0f, 5.0f});
    light_trans->transform.set_rotation({0.0f, -45.0f, -60.0f});
    
    auto* light = light_ent->add_component<DirectionalLightComponent>();
    light->set_color({1.0f, 1.0f, 1.0f});
    light->set_intensity(1.5f);
    light->set_enable(true);
    
    // Create bunny entity
    auto* bunny_ent = scene->create_entity();
    auto* bunny_trans = bunny_ent->add_component<TransformComponent>();
    bunny_trans->transform.set_scale({10.0f, 10.0f, 10.0f});
    
    // Load bunny model
    INFO(LogBunnyRender, "Loading bunny model from: {}", MODEL_PATH);
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    setting.load_materials = false;
    setting.flip_uv = false;
    setting.material_type = ModelMaterialType::PBR;
    
    auto bunny_model = Model::Load(MODEL_PATH, setting);
    if (!bunny_model || bunny_model->get_submesh_count() == 0) {
        ERR(LogBunnyRender, "Failed to load bunny model");
        return false;
    }
    
    INFO(LogBunnyRender, "Bunny model loaded: {} submeshes", bunny_model->get_submesh_count());
    
    // Add MeshRendererComponent
    auto* bunny_renderer = bunny_ent->add_component<MeshRendererComponent>();
    bunny_renderer->set_model(bunny_model);
    
    // Create and assign PBR material
    auto bunny_mat = std::make_shared<PBRMaterial>();
    bunny_mat->set_diffuse({0.8f, 0.5f, 0.3f, 1.0f});
    bunny_mat->set_roughness(0.2f);
    bunny_mat->set_metallic(0.8f);
    bunny_renderer->set_material(bunny_mat);
    
    // Save scene
    INFO(LogBunnyRender, "Saving scene to: {}", scene_path);
    auto am = EngineContext::asset();
    if (!am) {
        ERR(LogBunnyRender, "AssetManager is null");
        return false;
    }
    
    am->save_asset(scene, scene_path);
    
    auto saved_scene = am->get_asset_immediate(scene->get_uid());
    if (!saved_scene) {
        ERR(LogBunnyRender, "Failed to verify saved scene");
        return false;
    }
    
    INFO(LogBunnyRender, "Scene saved successfully, UID: {}", scene->get_uid().to_string());
    return true;
}

TEST_CASE("Render Bunny Model", "[draw][bunny]") {
    // Reset test state (Engine is already initialized by test_main.cpp)
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::render_system() != nullptr);
    REQUIRE(EngineContext::world() != nullptr);
    
    test_utils::RenderTestApp::Config config;
    config.scene_path = SCENE_PATH;
    config.width = 1280;
    config.height = 720;
    config.max_frames = 60;
    config.capture_frame = 30;
    config.create_scene_func = create_and_save_bunny_scene;
    
    std::vector<uint8_t> screenshot_data;
    int frames = 0;
    bool screenshot_taken = test_utils::RenderTestApp::run(config, screenshot_data, &frames);
    
    CHECK(frames > 0);
    
    // Save screenshot
    if (screenshot_taken) {
        std::string screenshot_path = test_asset_dir + "/bunny_screenshot.png";
        if (test_utils::save_screenshot_png(screenshot_path, config.width, config.height, screenshot_data)) {
            float brightness = test_utils::calculate_average_brightness(screenshot_data);
            INFO(LogBunnyRender, "Screenshot saved: {} (brightness: {:.1f})", screenshot_path, brightness);
            CHECK(brightness > 1.0f);
        }
    }
    
    // Reset state for next test
    test_utils::TestContext::reset();
}

TEST_CASE("Camera Movement", "[draw][bunny]") {
    // Reset test state (Engine is already initialized by test_main.cpp)
    test_utils::TestContext::reset();
    
    auto scene = std::make_shared<Scene>();
    
    auto* camera_ent = scene->create_entity();
    auto* cam_trans = camera_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({0.0f, 0.0f, 5.0f});
    
    auto* cam_comp = camera_ent->add_component<CameraComponent>();
    cam_comp->on_init();
    
    Vec3 initial_pos = cam_trans->transform.get_position();
    cam_comp->on_update(16.0f);
    Vec3 final_pos = cam_trans->transform.get_position();
    
    REQUIRE(cam_comp->get_position() == final_pos);
    
    // Reset state for next test
    test_utils::TestContext::reset();
}
