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
#include "engine/function/render/render_resource/material.h"
#include "engine/function/render/render_resource/texture.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/render/render_pass/pbr_forward_pass.h"
#include "engine/function/input/input.h"
#include "engine/core/log/Log.h"

#include <thread>
#include <chrono>
#include <fstream>
#include <vector>
#include <filesystem>

// stb_image_write is already implemented in test_bunny.cpp
#include <stb_image_write.h>

/**
 * @file test/draw/test_pbr_klee.cpp
 * @brief PBR rendering test using Klee model
 */

DEFINE_LOG_TAG(LogPBRKlee, "PBRKlee");

// Using Klee model for rendering test
static const std::string PBR_MODEL_PATH = "/Engine/models/Klee/klee.fbx";

static bool save_screenshot_png_klee(const std::string& filename, uint32_t width, uint32_t height, const std::vector<uint8_t>& data) {
    int stride_in_bytes = width * 4;
    int result = stbi_write_png(filename.c_str(), static_cast<int>(width), static_cast<int>(height), 4, data.data(), stride_in_bytes);
    return result != 0;
}

static float calculate_average_brightness_klee(const std::vector<uint8_t>& data) {
    if (data.empty()) return 0.0f;
    
    uint64_t total = 0;
    size_t pixel_count = data.size() / 4;
    
    for (size_t i = 0; i < pixel_count; i++) {
        uint32_t r = data[i * 4 + 0];
        uint32_t g = data[i * 4 + 1];
        uint32_t b = data[i * 4 + 2];
        total += (r + g + b) / 3;
    }
    
    return static_cast<float>(total) / static_cast<float>(pixel_count);
}

TEST_CASE("Load PBR FBX Model", "[draw][pbr]") {
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::render_system() != nullptr);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    REQUIRE(EngineContext::world() != nullptr);
    
    // Load model with materials (using AssetManager for caching)
    INFO(LogPBRKlee, "Loading PBR model from: {}", PBR_MODEL_PATH);
    
    auto pbr_model = Model::Load(PBR_MODEL_PATH, true, true, true);
    
    REQUIRE(pbr_model->get_submesh_count() > 0);
    
    INFO(LogPBRKlee, "PBR model loaded: {} submeshes", pbr_model->get_submesh_count());
    
    EngineContext::exit();
}

TEST_CASE("Render PBR Model", "[draw][pbr]") {
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
    
    // Enable PBR for better quality
    EngineContext::render_system()->get_mesh_manager()->set_pbr_enabled(true);
    
    // Create scene
    auto scene = std::make_shared<Scene>();
    
    // Create camera entity
    auto* camera_ent = scene->create_entity();
    REQUIRE(camera_ent != nullptr);
    
    auto* cam_trans = camera_ent->add_component<TransformComponent>();
    REQUIRE(cam_trans != nullptr);
    // Place camera at (-3, 0, 0) looking along +X axis toward origin
    cam_trans->transform.set_position({-3.0f, 0.0f, 0.0f});
    // Default rotation (0,0,0) looks along +X axis
    // Model at origin will be visible
    
    auto* cam_comp = camera_ent->add_component<CameraComponent>();
    REQUIRE(cam_comp != nullptr);
    cam_comp->set_fov(60.0f);
    cam_comp->set_far(100.0f);
    cam_comp->on_init();
    
    // Create directional light entity
    auto* light_ent = scene->create_entity();
    REQUIRE(light_ent != nullptr);
    
    auto* light_trans = light_ent->add_component<TransformComponent>();
    REQUIRE(light_trans != nullptr);
    light_trans->transform.set_position({100.0f, 200.0f, 100.0f});
    light_trans->transform.set_rotation({0.0f, -45.0f, -60.0f});
    
    auto* light_comp = light_ent->add_component<DirectionalLightComponent>();
    REQUIRE(light_comp != nullptr);
    light_comp->set_color({1.0f, 1.0f, 1.0f});
    light_comp->set_intensity(5.0f); // Increase intensity for PBR
    light_comp->set_enable(true);
    light_comp->on_init();
    
    // Create model entity
    auto* model_ent = scene->create_entity();
    REQUIRE(model_ent != nullptr);
    
    auto* model_trans = model_ent->add_component<TransformComponent>();
    REQUIRE(model_trans != nullptr);
    model_trans->transform.set_position({0.0f, 0.0f, 0.0f});
    model_trans->transform.set_rotation({0.0f, 0.0f, 0.0f});
    model_trans->transform.set_scale({1.0f, 1.0f, 1.0f});
    
    // Load PBR model with AssetManager caching
    INFO(LogPBRKlee, "Loading PBR model from: {}", PBR_MODEL_PATH);
    
    auto pbr_model = Model::Load(PBR_MODEL_PATH, true, true, true);
    
    REQUIRE(pbr_model->get_submesh_count() > 0);
    
    INFO(LogPBRKlee, "PBR model loaded: {} submeshes", pbr_model->get_submesh_count());
    
    // Add MeshRendererComponent
    auto* model_mesh = model_ent->add_component<MeshRendererComponent>();
    REQUIRE(model_mesh != nullptr);
    model_mesh->set_model(pbr_model);
    model_mesh->on_init();
    
    // Set active scene
    EngineContext::world()->set_active_scene(scene);
    EngineContext::render_system()->get_mesh_manager()->set_active_camera(cam_comp);
    
    int frames = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    const uint32_t screenshot_width = 1280;
    const uint32_t screenshot_height = 720;
    std::vector<uint8_t> screenshot_data(screenshot_width * screenshot_height * 4);
    bool screenshot_taken = false;
    
    while (frames < 60) {
        Input::get_instance().tick();
        EngineContext::world()->tick(0.016f);
        
        RenderPacket packet;
        packet.active_camera = cam_comp;
        packet.active_scene = scene.get();
        packet.frame_index = frames % 2; // Simple ping-pong for frames in flight
        
        bool should_continue = EngineContext::render_system()->tick(packet);
        if (!should_continue) {
            break;
        }
        
        frames++;
        
        // Take screenshot on frame 45 (give PBR time to stabilize)
        if (frames == 45 && !screenshot_taken) {
            auto swapchain = EngineContext::render_system()->get_swapchain();
            if (swapchain) {
                uint32_t current_frame = swapchain->get_current_frame_index();
                RHITextureRef back_buffer = swapchain->get_texture(current_frame);
                if (back_buffer) {
                    auto backend = EngineContext::rhi();
                    auto pool = backend->create_command_pool({});
                    auto context = backend->create_command_context(pool);
                    
                    // Flush any pending commands
                    context->begin_command();
                    context->end_command();
                    auto fence = backend->create_fence(false);
                    context->execute(fence, nullptr, nullptr);
                    fence->wait();
                    
                    if (context->read_texture(back_buffer, screenshot_data.data(), screenshot_data.size())) {
                        screenshot_taken = true;
                    }
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    CHECK(frames > 0);
    
    // Save screenshot
    if (screenshot_taken) {
        std::string screenshot_path = test_asset_dir + "/klee_pbr_screenshot.png";
        if (save_screenshot_png_klee(screenshot_path, screenshot_width, screenshot_height, screenshot_data)) {
            float brightness = calculate_average_brightness_klee(screenshot_data);
            INFO(LogPBRKlee, "Screenshot saved: {} (brightness: {:.1f})", screenshot_path, brightness);
            CHECK(brightness > 0.0f);
        }
    }
    
    EngineContext::world()->set_active_scene(nullptr);
    EngineContext::exit();
}

TEST_CASE("PBR Forward Pass Basic", "[draw][pbr]") {
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    REQUIRE(EngineContext::rhi() != nullptr);
    
    // Create PBR forward pass
    auto pbr_pass = std::make_shared<render::PBRForwardPass>();
    pbr_pass->init();
    
    REQUIRE(pbr_pass->is_initialized());
    
    // Test setting per-frame data
    Mat4 view = Mat4::Identity();
    Mat4 proj = Mat4::Identity();
    Vec3 camera_pos(0.0f, 0.0f, 5.0f);
    Vec3 light_dir(0.5f, -0.5f, 0.5f);
    Vec3 light_color(1.0f, 1.0f, 1.0f);
    
    pbr_pass->set_per_frame_data(view, proj, camera_pos, light_dir, light_color, 1.0f);
    
    // Test point lights
    pbr_pass->add_point_light(Vec3(1.0f, 1.0f, 1.0f), Vec3(1.0f, 0.5f, 0.0f), 1.0f, 10.0f);
    pbr_pass->add_point_light(Vec3(-1.0f, 1.0f, -1.0f), Vec3(0.0f, 0.5f, 1.0f), 0.5f, 5.0f);
    
    // Must destroy pass BEFORE EngineContext::exit() to avoid accessing destroyed RHI backend
    pbr_pass.reset();
    
    EngineContext::exit();
}
