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
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/input/input.h"
#include "engine/core/log/Log.h"
#include "engine/function/asset/asset_manager.h"

#include <thread>
#include <chrono>
#include <vector>

/**
 * @file test/render/test_npr.cpp
 * @brief NPR (Non-Photorealistic Rendering) tests using Klee model.
 */

DEFINE_LOG_TAG(LogNPRTest, "NPR");

static const std::string KLEE_MODEL_PATH = "/Engine/models/Klee/klee.fbx";
static const std::string NPR_SCENE_PATH = "/Game/npr_klee_test.asset";

static bool create_npr_scene(const std::string& scene_path) {
    INFO(LogNPRTest, "Creating NPR scene");
    
    auto scene = std::make_shared<Scene>();
    
    // Create camera entity
    auto* camera_ent = scene->create_entity();
    auto* cam_trans = camera_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({-30.0f, 10.0f, 0.0f});
    cam_trans->transform.set_rotation({0.0f, -15.0f, 0.0f});
    
    auto* camera = camera_ent->add_component<CameraComponent>();
    camera->set_fov(60.0f);
    camera->set_far(1000.0f);
    
    // Create directional light entity
    auto* light_ent = scene->create_entity();
    auto* light_trans = light_ent->add_component<TransformComponent>();
    light_trans->transform.set_position({100.0f, 200.0f, 100.0f});
    light_trans->transform.set_rotation({0.0f, -45.0f, -60.0f});
    
    auto* light = light_ent->add_component<DirectionalLightComponent>();
    light->set_color({1.0f, 1.0f, 1.0f});
    light->set_intensity(100.0f);
    light->set_enable(true);
    
    // Create model entity
    auto* model_ent = scene->create_entity();
    auto* model_trans = model_ent->add_component<TransformComponent>();
    model_trans->transform.set_position({0.0f, 0.0f, 0.0f});
    model_trans->transform.set_scale({1.0f, 1.0f, 1.0f});
    
    // Load NPR model with NPR material type
    INFO(LogNPRTest, "Loading NPR model from: {}", KLEE_MODEL_PATH);
    
    ModelProcessSetting npr_setting;
    npr_setting.smooth_normal = true;
    npr_setting.load_materials = true;
    npr_setting.flip_uv = false;
    npr_setting.material_type = ModelMaterialType::NPR;
    
    auto npr_model = Model::Load(KLEE_MODEL_PATH, npr_setting);
    if (!npr_model || npr_model->get_submesh_count() == 0) {
        ERR(LogNPRTest, "Failed to load Klee model");
        return false;
    }
    
    INFO(LogNPRTest, "NPR model loaded: {} submeshes", npr_model->get_submesh_count());
    
    // Add MeshRendererComponent
    auto* model_mesh = model_ent->add_component<MeshRendererComponent>();
    model_mesh->set_model(npr_model);
    
    // Auto-adjust camera to model bounding box
    auto box = npr_model->get_bounding_box();
    Vec3 center = (box.min + box.max) * 0.5f;
    float size = (box.max - box.min).norm();
    
    float dist = size * 1.5f;
    if (dist < 1.0f) dist = 5.0f;
    
    cam_trans->transform.set_position(center + Vec3(-dist, size * 0.5f, 0.0f));
    
    INFO(LogNPRTest, "Model bounds: min=({},{},{}), max=({},{},{}), size={}",
         box.min.x(), box.min.y(), box.min.z(), 
         box.max.x(), box.max.y(), box.max.z(), size);
    
    // Save scene
    INFO(LogNPRTest, "Saving scene to: {}", scene_path);
    auto am = EngineContext::asset();
    if (!am) {
        ERR(LogNPRTest, "AssetManager is null");
        return false;
    }
    
    am->save_asset(scene, scene_path);
    
    auto saved_scene = am->get_asset_immediate(scene->get_uid());
    if (!saved_scene) {
        ERR(LogNPRTest, "Failed to verify saved scene");
        return false;
    }
    
    INFO(LogNPRTest, "Scene saved successfully, UID: {}", scene->get_uid().to_string());
    return true;
}

static void on_scene_loaded(test_utils::SceneLoadResult& result) {
    // Enable NPR for this scene
    if (auto mesh_manager = EngineContext::render_system()->get_mesh_manager()) {
        mesh_manager->set_npr_enabled(true);
        mesh_manager->set_active_camera(result.camera);
    }
}

TEST_CASE("NPR Klee rendering", "[npr]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::render_system() != nullptr);
    REQUIRE(EngineContext::world() != nullptr);
    
    test_utils::RenderTestApp::Config config;
    config.scene_path = NPR_SCENE_PATH;
    config.width = 1280;
    config.height = 720;
    config.max_frames = 6000;
    config.capture_frame = 45;  // Capture at frame 45 for NPR
    config.create_scene_func = create_npr_scene;
    
    config.on_scene_loaded_func = on_scene_loaded;
    
    std::vector<uint8_t> screenshot;
    int frames = 0;
    bool captured = test_utils::RenderTestApp::run(config, screenshot, &frames);
    
    CHECK(frames > 0);
    
    if (captured) {
        std::string screenshot_path = test_asset_dir + "/klee_npr.png";
        if (test_utils::save_screenshot_png(screenshot_path, config.width, config.height, screenshot)) {
            float brightness = test_utils::calculate_average_brightness(screenshot);
            INFO(LogNPRTest, "Screenshot saved: {} (brightness: {:.1f})", screenshot_path, brightness);
            CHECK(brightness > 0.0f);
        }
    }
    
    test_utils::TestContext::reset();
}
