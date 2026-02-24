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
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/render/render_pass/g_buffer_pass.h"
#include "engine/function/render/render_pass/deferred_lighting_pass.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"

/**
 * @file test/render/test_pbr.cpp
 * @brief PBR rendering tests using deferred pipeline.
 */

DEFINE_LOG_TAG(LogPBRTest, "PBR");

static const std::string PBR_SCENE_PATH = "/Game/pbr_scene.asset";
static const std::string MODEL_PATH = "/Engine/models/material_ball/material_ball.fbx";

static bool create_pbr_scene(const std::string& scene_path) {
    auto scene = std::make_shared<Scene>();
    
    // Camera
    auto* cam_ent = scene->create_entity();
    auto* cam_trans = cam_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({0.0f, 1.0f, 3.0f});
    cam_trans->transform.set_rotation({-15.0f, 0.0f, 0.0f});
    
    auto* cam = cam_ent->add_component<CameraComponent>();
    cam->set_fov(60.0f);
    
    // Light
    auto* light_ent = scene->create_entity();
    auto* light_trans = light_ent->add_component<TransformComponent>();
    light_trans->transform.set_position({5.0f, 10.0f, 5.0f});
    light_trans->transform.set_rotation({45.0f, -45.0f, 0.0f});
    
    auto* light = light_ent->add_component<DirectionalLightComponent>();
    light->set_color({1.0f, 0.98f, 0.95f});
    light->set_intensity(2.0f);
    light->set_enable(true);
    
    // Model
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    setting.load_materials = true;
    
    auto model = Model::Load(MODEL_PATH, setting);
    if (!model || model->get_submesh_count() == 0) {
        ERR(LogPBRTest, "Failed to load model");
        return false;
    }
    
    auto* model_ent = scene->create_entity();
    auto* model_trans = model_ent->add_component<TransformComponent>();
    model_trans->transform.set_position({0.0f, 0.0f, 0.0f});
    
    auto* mesh = model_ent->add_component<MeshRendererComponent>();
    mesh->set_model(model);
    
    INFO(LogPBRTest, "Scene created with {} submeshes", model->get_submesh_count());
    
    EngineContext::asset()->save_asset(scene, scene_path);
    return true;
}

TEST_CASE("PBR deferred rendering", "[pbr]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::render_system() != nullptr);
    
    // Init deferred passes
    auto gbuffer = std::make_shared<render::GBufferPass>();
    auto lighting = std::make_shared<render::DeferredLightingPass>();
    gbuffer->init();
    lighting->init();
    
    REQUIRE(gbuffer->is_ready());
    REQUIRE(lighting->is_ready());
    
    test_utils::RenderTestApp::Config config;
    config.scene_path = PBR_SCENE_PATH;
    config.width = 1280;
    config.height = 720;
    config.max_frames = 60;
    config.capture_frame = 30;
    config.create_scene_func = create_pbr_scene;
    
    std::vector<uint8_t> screenshot;
    int frames = 0;
    bool captured = test_utils::RenderTestApp::run(config, screenshot, &frames);
    
    CHECK(frames > 0);
    
    if (captured) {
        std::string path = test_asset_dir + "/pbr_deferred.png";
        if (test_utils::save_screenshot_png(path, config.width, config.height, screenshot)) {
            float brightness = test_utils::calculate_average_brightness(screenshot);
            INFO(LogPBRTest, "Screenshot saved: {} (brightness: {:.1f})", path, brightness);
            CHECK(brightness > 1.0f);
            CHECK(brightness < 255.0f);
        }
    }
    
    test_utils::TestContext::reset();
}

TEST_CASE("PBR material params", "[pbr]") {
    test_utils::TestContext::reset();
    
    auto mat = std::make_shared<PBRMaterial>();
    
    mat->set_diffuse({0.8f, 0.5f, 0.3f, 1.0f});
    mat->set_roughness(0.5f);
    mat->set_metallic(0.8f);
    
    CHECK(mat->get_diffuse().x() == Catch::Approx(0.8f));
    CHECK(mat->get_roughness() == Catch::Approx(0.5f));
    CHECK(mat->get_metallic() == Catch::Approx(0.8f));
    
    test_utils::TestContext::reset();
}
