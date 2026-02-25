#include <catch2/catch_test_macros.hpp>
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
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"

/**
 * @file test/render/test_rdg.cpp
 * @brief RDG forward rendering tests.
 */

DEFINE_LOG_TAG(LogRDGTest, "RDG");

static const std::string RDG_SCENE_PATH = "/Game/rdg_scene.asset";
static const std::string MODEL_PATH = "/Engine/models/bunny.obj";

static bool create_rdg_scene(const std::string& scene_path) {
    auto scene = std::make_shared<Scene>();
    
    // Camera
    auto* cam_ent = scene->create_entity();
    auto* cam_trans = cam_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({0.0f, 0.0f, 3.0f});
    
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
    
    // Model
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    setting.load_materials = false;
    
    auto model = Model::Load(MODEL_PATH, setting);
    if (!model || model->get_submesh_count() == 0) {
        ERR(LogRDGTest, "Failed to load model");
        return false;
    }
    
    auto* model_ent = scene->create_entity();
    auto* model_trans = model_ent->add_component<TransformComponent>();
    model_trans->transform.set_scale({10.0f, 10.0f, 10.0f});
    
    auto* mesh = model_ent->add_component<MeshRendererComponent>();
    mesh->set_model(model);
    
    INFO(LogRDGTest, "Scene created with {} submeshes", model->get_submesh_count());
    
    EngineContext::asset()->save_asset(scene, scene_path);
    return true;
}

TEST_CASE("RDG forward rendering", "[rdg]") {
    test_utils::TestContext::reset();
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::render_system() != nullptr);
    REQUIRE(EngineContext::world() != nullptr);
    
    test_utils::RenderTestApp::Config config;
    config.scene_path = RDG_SCENE_PATH;
    config.width = 1280;
    config.height = 720;
    config.max_frames = 60;
    config.capture_frame = 0;  // No screenshot for RDG test
    config.create_scene_func = create_rdg_scene;
    
    std::vector<uint8_t> screenshot;
    int frames = 0;
    test_utils::RenderTestApp::run(config, screenshot, &frames);
    
    CHECK(frames == 60);
    
    test_utils::TestContext::reset();
}
