#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
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
#include "engine/function/render/render_pass/g_buffer_pass.h"
#include "engine/function/render/render_pass/deferred_lighting_pass.h"
#include "engine/core/log/Log.h"

#include <thread>
#include <chrono>

#include <stb_image_write.h>

/**
 * @file test/draw/test_pbr_deferred.cpp
 * @brief Tests for PBR Deferred Rendering pipeline
 */

DEFINE_LOG_TAG(LogPbrDeferred, "PbrDeferred");

// Forward declarations from test_bunny.cpp
extern bool save_screenshot_png(const std::string& filename, uint32_t width, uint32_t height, const std::vector<uint8_t>& data);
extern float calculate_average_brightness(const std::vector<uint8_t>& data);

TEST_CASE("GBuffer Pass Initialization", "[pbr][deferred]") {
    INFO(LogPbrDeferred, "Testing GBuffer Pass initialization...");
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    // Create GBuffer pass
    auto gbuffer_pass = std::make_shared<render::GBufferPass>();
    gbuffer_pass->init();
    
    REQUIRE(gbuffer_pass->is_ready());
    INFO(LogPbrDeferred, "GBuffer Pass initialized successfully");
    
    EngineContext::exit();
}

TEST_CASE("Deferred Lighting Pass Initialization", "[pbr][deferred]") {
    INFO(LogPbrDeferred, "Testing Deferred Lighting Pass initialization...");
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    // Create Deferred Lighting pass
    auto lighting_pass = std::make_shared<render::DeferredLightingPass>();
    lighting_pass->init();
    
    REQUIRE(lighting_pass->is_ready());
    INFO(LogPbrDeferred, "Deferred Lighting Pass initialized successfully");
    
    EngineContext::exit();
}

TEST_CASE("PBR Deferred Rendering - Material Ball", "[pbr][deferred]") {
    INFO(LogPbrDeferred, "Starting PBR deferred rendering test with material ball...");
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    EngineContext::asset()->init(test_asset_dir);
    
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::render_system() != nullptr);
    
    auto render_system = EngineContext::render_system();
    
    // Create GBuffer and Deferred Lighting passes
    auto gbuffer_pass = std::make_shared<render::GBufferPass>();
    gbuffer_pass->init();
    
    auto lighting_pass = std::make_shared<render::DeferredLightingPass>();
    lighting_pass->init();
    
    REQUIRE(gbuffer_pass->is_ready());
    REQUIRE(lighting_pass->is_ready());
    
    INFO(LogPbrDeferred, "PBR passes initialized");
    
    // Create scene
    auto scene = std::make_shared<Scene>();
    
    // Create camera
    auto* camera_ent = scene->create_entity();
    auto* cam_trans = camera_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({0.0f, 1.0f, 3.0f});
    cam_trans->transform.set_rotation({-15.0f, 0.0f, 0.0f});
    
    auto* cam_comp = camera_ent->add_component<CameraComponent>();
    cam_comp->set_fov(60.0f);
    cam_comp->on_init();
    
    // Create directional light
    auto* light_ent = scene->create_entity();
    auto* light_trans = light_ent->add_component<TransformComponent>();
    light_trans->transform.set_position({5.0f, 10.0f, 5.0f});
    light_trans->transform.set_rotation({45.0f, -45.0f, 0.0f});
    
    auto* light_comp = light_ent->add_component<DirectionalLightComponent>();
    light_comp->set_color({1.0f, 0.98f, 0.95f});
    light_comp->set_intensity(2.0f);
    light_comp->set_enable(true);
    light_comp->on_init();
    
    // Load material ball model
    std::string model_path = std::string(ENGINE_PATH) + "/assets/models/material_ball.fbx";
    
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    setting.load_materials = true;
    
    auto model = std::make_shared<Model>(model_path, setting);
    REQUIRE(model->get_submesh_count() > 0);
    
    // Create model entity
    auto* model_ent = scene->create_entity();
    auto* model_trans = model_ent->add_component<TransformComponent>();
    model_trans->transform.set_position({0.0f, 0.0f, 0.0f});
    model_trans->transform.set_scale({1.0f, 1.0f, 1.0f});
    
    auto* mesh_comp = model_ent->add_component<MeshRendererComponent>();
    mesh_comp->set_model(model);
    mesh_comp->on_init();
    
    INFO(LogPbrDeferred, "Model loaded: {} submeshes", model->get_submesh_count());
    
    // Set active scene and camera
    EngineContext::world()->set_active_scene(scene);
    render_system->get_mesh_manager()->set_active_camera(cam_comp);
    
    // Render frames
    const uint32_t screenshot_width = 1280;
    const uint32_t screenshot_height = 720;
    std::vector<uint8_t> screenshot_data(screenshot_width * screenshot_height * 4);
    bool screenshot_taken = false;
    
    INFO(LogPbrDeferred, "Starting render loop...");
    
    for (int frame = 0; frame < 60; frame++) {
        EngineContext::world()->tick(0.016f);
        
        RenderPacket packet;
        packet.active_camera = cam_comp;
        packet.active_scene = scene.get();
        
        bool should_continue = render_system->tick(packet);
        if (!should_continue) break;
        
        // Take screenshot on frame 30
        if (frame == 30 && !screenshot_taken) {
            auto swapchain = render_system->get_swapchain();
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
                        INFO(LogPbrDeferred, "Screenshot captured");
                    }
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    // Save screenshot
    if (screenshot_taken) {
        std::string screenshot_path = test_asset_dir + "/pbr_material_ball.png";
        if (save_screenshot_png(screenshot_path, screenshot_width, screenshot_height, screenshot_data)) {
            INFO(LogPbrDeferred, "Screenshot saved to: {}", screenshot_path);
            
            float brightness = calculate_average_brightness(screenshot_data);
            INFO(LogPbrDeferred, "Screenshot average brightness: {}", brightness);
            
            CHECK(brightness > 1.0f);
            CHECK(brightness < 255.0f);
        }
    }
    
    EngineContext::world()->set_active_scene(nullptr);
    EngineContext::exit();
    
    INFO(LogPbrDeferred, "PBR deferred rendering test completed");
}
