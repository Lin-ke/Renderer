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
#include "engine/function/render/render_pass/npr_forward_pass.h"
#include "engine/function/input/input.h"
#include "engine/core/log/Log.h"
#include "engine/function/asset/asset_manager.h"

#include <thread>
#include <chrono>
#include <vector>

DEFINE_LOG_TAG(LogPBRKlee, "PBRKlee");

static const std::string PBR_MODEL_PATH = "/Engine/models/Klee/klee.fbx";
static const std::string SCENE_SAVE_PATH = "/Game/npr_klee_test.asset";

/**
 * @brief Part 1: Create and setup test scene, save to file
 */
static bool create_and_save_npr_scene(const std::string& scene_path) {
    INFO(LogPBRKlee, "=== Part 1: Creating Scene ===");
    
    auto scene = std::make_shared<Scene>();
    
    // Create camera entity
    auto* camera_ent = scene->create_entity();
    auto* cam_trans = camera_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({-30.0f, 10.0f, 0.0f});
    cam_trans->transform.set_rotation({0.0f, -15.0f, 0.0f});
    
    auto* camera = camera_ent->add_component<CameraComponent>();
    camera->set_fov(60.0f);
    camera->set_far(1000.0f);
    camera->on_init();
    
    // Create directional light entity
    auto* light_ent = scene->create_entity();
    auto* light_trans = light_ent->add_component<TransformComponent>();
    light_trans->transform.set_position({100.0f, 200.0f, 100.0f});
    light_trans->transform.set_rotation({0.0f, -45.0f, -60.0f});
    
    auto* light = light_ent->add_component<DirectionalLightComponent>();
    light->set_color({1.0f, 1.0f, 1.0f});
    light->set_intensity(100.0f);
    light->set_enable(true);
    light->on_init();
    
    // Create model entity
    auto* model_ent = scene->create_entity();
    auto* model_trans = model_ent->add_component<TransformComponent>();
    model_trans->transform.set_position({0.0f, 0.0f, 0.0f});
    model_trans->transform.set_scale({1.0f, 1.0f, 1.0f});
    
    // Load NPR model
    INFO(LogPBRKlee, "Loading NPR model from: {}", PBR_MODEL_PATH);
    
    ModelProcessSetting npr_setting;
    npr_setting.smooth_normal = true;
    npr_setting.load_materials = true;
    npr_setting.flip_uv = true;
    npr_setting.material_type = ModelMaterialType::NPR;
    
    auto npr_model = Model::Load(PBR_MODEL_PATH, npr_setting);
    if (npr_model->get_submesh_count() == 0) return false;
    
    INFO(LogPBRKlee, "NPR model loaded: {} submeshes", npr_model->get_submesh_count());
    
    // Add MeshRendererComponent
    auto* model_mesh = model_ent->add_component<MeshRendererComponent>();
    model_mesh->set_model(npr_model);
    model_mesh->on_init();
    
    // Auto-adjust camera to model bounding box
    auto box = npr_model->get_bounding_box();
    Vec3 center = (box.min + box.max) * 0.5f;
    float size = (box.max - box.min).norm();
    
    float dist = size * 1.5f;
    if (dist < 1.0f) dist = 5.0f;
    
    cam_trans->transform.set_position(center + Vec3(-dist, size * 0.5f, 0.0f));
    
    INFO(LogPBRKlee, "Model bounds: min=({},{},{}), max=({},{},{}), size={}",
         box.min.x(), box.min.y(), box.min.z(), 
         box.max.x(), box.max.y(), box.max.z(), size);
    
    // Save scene
    INFO(LogPBRKlee, "Saving scene to: {}", scene_path);
    auto am = EngineContext::asset();
    if (!am) {
        ERR(LogPBRKlee, "AssetManager is null");
        return false;
    }
    
    am->save_asset(scene, scene_path);
    
    auto saved_scene = am->get_asset_immediate(scene->get_uid());
    if (!saved_scene) {
        ERR(LogPBRKlee, "Failed to verify saved scene");
        return false;
    }
    
    INFO(LogPBRKlee, "Scene saved successfully, UID: {}", scene->get_uid().to_string());
    return true;
}

/**
 * @brief Part 2: Load scene from file
 */
static test_utils::SceneLoadResult load_npr_scene(const std::string& scene_path) {
    INFO(LogPBRKlee, "=== Part 2: Loading Scene ===");
    INFO(LogPBRKlee, "Loading scene from: {}", scene_path);
    
    // Use SceneLoader utility
    auto result = test_utils::SceneLoader::load(scene_path, true, false);
    
    if (!result.success) {
        ERR(LogPBRKlee, "Failed to load scene: {}", result.error_msg);
        return result;
    }
    
    INFO(LogPBRKlee, "Scene loaded, entities: {}", result.scene->entities_.size());
    
    // Enable NPR for this scene
    if (auto mesh_manager = EngineContext::render_system()->get_mesh_manager()) {
        mesh_manager->set_npr_enabled(true);
        mesh_manager->set_active_camera(result.camera);
    }
    EngineContext::render_system()->set_prepass_enabled(true);
    
    EngineContext::world()->set_active_scene(result.scene);
    
    return result;
}

/**
 * @brief Part 3: Render frames and capture screenshot
 */
static bool render_npr_frames(CameraComponent* camera, Scene* scene, 
                               std::vector<uint8_t>& screenshot_data, 
                               uint32_t screenshot_width, uint32_t screenshot_height,
                               int& out_frames) {
    INFO(LogPBRKlee, "=== Part 3: Rendering ===");
    
    int frames = 0;
    bool screenshot_taken = false;
    
    while (frames < 60) {
        Input::get_instance().tick();
        EngineContext::world()->tick(0.016f);
        
        RenderPacket packet;
        packet.active_camera = camera;
        packet.active_scene = scene;
        packet.frame_index = frames % 2;
        
        bool should_continue = EngineContext::render_system()->tick(packet);
        if (!should_continue) break;
        
        frames++;
        
        // Take screenshot on frame 45
        if (frames == 45 && !screenshot_taken) {
            auto swapchain = EngineContext::render_system()->get_swapchain();
            if (swapchain) {
                uint32_t current_frame = swapchain->get_current_frame_index();
                RHITextureRef back_buffer = swapchain->get_texture(current_frame);
                if (back_buffer) {
                    auto backend = EngineContext::rhi();
                    auto pool = backend->create_command_pool({});
                    auto context = backend->create_command_context(pool);
                    
                    context->begin_command();
                    context->end_command();
                    auto fence = backend->create_fence(false);
                    context->execute(fence, nullptr, nullptr);
                    fence->wait();
                    
                    if (context->read_texture(back_buffer, screenshot_data.data(), screenshot_data.size())) {
                        screenshot_taken = true;
                        INFO(LogPBRKlee, "Screenshot captured at frame {}", frames);
                    }
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    out_frames = frames;
    INFO(LogPBRKlee, "Rendering complete, total frames: {}", frames);
    return screenshot_taken;
}

TEST_CASE("Render NPR Model", "[draw][npr]") {
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    EngineContext::asset()->init(test_asset_dir);
    
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::render_system() != nullptr);
    REQUIRE(EngineContext::world() != nullptr);
    
    // ============================================================================
    // Part 1: Create Scene (Comment this out after first run to skip creation)
    // ============================================================================
    // bool create_success = create_and_save_npr_scene(SCENE_SAVE_PATH);
    // REQUIRE(create_success);
    
    // ============================================================================
    // Part 2: Load Scene
    // ============================================================================
    auto result = load_npr_scene(SCENE_SAVE_PATH);
    REQUIRE(result.success);
    REQUIRE(result.camera != nullptr);
    REQUIRE(result.scene != nullptr);
    
    // ============================================================================
    // Part 3: Render
    // ============================================================================
    const uint32_t screenshot_width = 1280;
    const uint32_t screenshot_height = 720;
    std::vector<uint8_t> screenshot_data(screenshot_width * screenshot_height * 4);
    
    int frames = 0;
    bool screenshot_taken = render_npr_frames(
        result.camera, 
        result.scene.get(), 
        screenshot_data, 
        screenshot_width, 
        screenshot_height,
        frames
    );
    
    CHECK(frames > 0);
    
    // Save screenshot
    if (screenshot_taken) {
        std::string screenshot_path = test_asset_dir + "/klee_npr_screenshot.png";
        if (test_utils::save_screenshot_png(screenshot_path, screenshot_width, screenshot_height, screenshot_data)) {
            float brightness = test_utils::calculate_average_brightness(screenshot_data);
            INFO(LogPBRKlee, "Screenshot saved: {} (brightness: {:.1f})", screenshot_path, brightness);
            CHECK(brightness > 0.0f);
        }
    }
    
    EngineContext::world()->set_active_scene(nullptr);
    EngineContext::exit();
}
