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
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/render/render_pass/g_buffer_pass.h"
#include "engine/function/render/render_pass/deferred_lighting_pass.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/input/input.h"
#include "engine/core/log/Log.h"

#include <thread>
#include <chrono>
#include <vector>
#include <string>

/**
 * @file test/draw/test_draw_advanced.cpp
 * @brief Advanced rendering tests including deferred rendering and RDG forward rendering.
 */

DEFINE_LOG_TAG(LogDrawAdvanced, "DrawAdvanced");

// ==================== PBR Deferred Rendering Tests ====================

TEST_CASE("PBR Deferred Rendering Tests", "[draw][advanced][pbr][deferred]") {
    SECTION("GBuffer Pass Initialization") {
        test_utils::TestContext::reset();
        
        // Create GBuffer pass
        auto gbuffer_pass = std::make_shared<render::GBufferPass>();
        gbuffer_pass->init();
        
        REQUIRE(gbuffer_pass->is_ready());
        
        test_utils::TestContext::reset();
    }

    SECTION("Deferred Lighting Pass Initialization") {
        test_utils::TestContext::reset();
        
        // Create Deferred Lighting pass
        auto lighting_pass = std::make_shared<render::DeferredLightingPass>();
        lighting_pass->init();
        
        REQUIRE(lighting_pass->is_ready());
        
        test_utils::TestContext::reset();
    }

    SECTION("PBR Deferred Rendering - Material Ball") {
        test_utils::TestContext::reset();
        
        std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
        
        if (auto am = EngineContext::asset()) {
            am->init(test_asset_dir);
        }
        
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
        std::string model_path = "/Engine/models/material_ball/material_ball.fbx";
        
        ModelProcessSetting setting;
        setting.smooth_normal = true;
        setting.load_materials = true;
        
        auto model = Model::Load(model_path, setting);
        REQUIRE(model != nullptr);
        REQUIRE(model->get_submesh_count() > 0);
        
        // Create model entity
        auto* model_ent = scene->create_entity();
        auto* model_trans = model_ent->add_component<TransformComponent>();
        model_trans->transform.set_position({0.0f, 0.0f, 0.0f});
        model_trans->transform.set_scale({1.0f, 1.0f, 1.0f});
        
        auto* mesh_comp = model_ent->add_component<MeshRendererComponent>();
        mesh_comp->set_model(model);
        mesh_comp->on_init();
        
        INFO(LogDrawAdvanced, "Loaded {} submeshes", model->get_submesh_count());
        
        // Set active scene and camera
        EngineContext::world()->set_active_scene(scene);
        render_system->get_mesh_manager()->set_active_camera(cam_comp);
        
        // Render frames
        const uint32_t screenshot_width = 1280;
        const uint32_t screenshot_height = 720;
        std::vector<uint8_t> screenshot_data(screenshot_width * screenshot_height * 4);
        bool screenshot_taken = false;
        
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
                        }
                    }
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        
        // Save screenshot
        if (screenshot_taken) {
            std::string screenshot_path = test_asset_dir + "/pbr_material_ball.png";
            if (test_utils::save_screenshot_png(screenshot_path, screenshot_width, screenshot_height, screenshot_data)) {
                float brightness = test_utils::calculate_average_brightness(screenshot_data);
                INFO(LogDrawAdvanced, "Screenshot saved: {} (brightness: {:.1f})", screenshot_path, brightness);
                CHECK(brightness > 1.0f);
                CHECK(brightness < 255.0f);
            }
        }
        
        test_utils::TestContext::reset();
    }
}

// ==================== RDG Forward Rendering Tests ====================

TEST_CASE("RDG Forward Rendering Tests", "[draw][advanced][rdg]") {
    SECTION("RDG Forward Pass - Bunny Rendering") {
        test_utils::TestContext::reset();
        
        std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
        
        if (auto am = EngineContext::asset()) {
            am->init(test_asset_dir);
        }
        
        REQUIRE(EngineContext::rhi() != nullptr);
        REQUIRE(EngineContext::render_system() != nullptr);
        REQUIRE(EngineContext::world() != nullptr);
        
        // Create scene
        auto scene = std::make_shared<Scene>();
        
        // Create camera
        auto* camera_ent = scene->create_entity();
        auto* cam_trans = camera_ent->add_component<TransformComponent>();
        cam_trans->transform.set_position({0.0f, 0.0f, 3.0f});
        
        auto* cam_comp = camera_ent->add_component<CameraComponent>();
        cam_comp->set_fov(60.0f);
        cam_comp->on_init();
        
        // Create directional light
        auto* light_ent = scene->create_entity();
        auto* light_trans = light_ent->add_component<TransformComponent>();
        light_trans->transform.set_position({5.0f, 10.0f, 5.0f});
        light_trans->transform.set_rotation({0.0f, -45.0f, -60.0f});
        
        auto* light_comp = light_ent->add_component<DirectionalLightComponent>();
        light_comp->set_color({1.0f, 1.0f, 1.0f});
        light_comp->set_intensity(1.5f);
        light_comp->set_enable(true);
        light_comp->on_init();
        
        // Create bunny entity
        auto* bunny_ent = scene->create_entity();
        auto* bunny_trans = bunny_ent->add_component<TransformComponent>();
        bunny_trans->transform.set_position({0.0f, 0.0f, 0.0f});
        bunny_trans->transform.set_scale({10.0f, 10.0f, 10.0f});
        
        auto* bunny_mesh = bunny_ent->add_component<MeshRendererComponent>();
        
        // Load bunny model
        ModelProcessSetting setting;
        setting.smooth_normal = true;
        setting.load_materials = false;
        
        std::string model_path = "/Engine/models/bunny.obj";
        auto bunny_model = Model::Load(model_path, setting);
        
        REQUIRE(bunny_model != nullptr);
        REQUIRE(bunny_model->get_submesh_count() > 0);
        
        bunny_mesh->set_model(bunny_model);
        bunny_mesh->on_init();
        
        auto mesh0 = bunny_model->get_mesh(0);
        if (mesh0) {
            INFO(LogDrawAdvanced, "Bunny loaded: {} vertices, {} indices",
                 mesh0->get_vertex_count(),
                 mesh0->get_index_count());
        }
        
        // Set active scene
        EngineContext::world()->set_active_scene(scene);
        EngineContext::render_system()->get_mesh_manager()->set_active_camera(cam_comp);
        
        void* window_handle = EngineContext::render_system()->get_window_handle();
        REQUIRE(window_handle != nullptr);
        
        int frames = 0;
        auto start_time = std::chrono::steady_clock::now();
        
        while (frames < 60) {
            Input::get_instance().tick();
            EngineContext::world()->tick(0.016f);
            
            RenderPacket packet;
            packet.active_camera = cam_comp;
            packet.active_scene = scene.get();
            
            bool should_continue = EngineContext::render_system()->tick(packet);
            if (!should_continue) {
                break;
            }
            
            frames++;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        CHECK(frames > 0);
        
        test_utils::TestContext::reset();
    }

    SECTION("RDG Forward Pass - Wireframe Toggle") {
        test_utils::TestContext::reset();
        
        std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
        
        if (auto am = EngineContext::asset()) {
            am->init(test_asset_dir);
        }
        
        auto scene = std::make_shared<Scene>();
        
        // Create camera
        auto* camera_ent = scene->create_entity();
        auto* cam_trans = camera_ent->add_component<TransformComponent>();
        cam_trans->transform.set_position({0.0f, 0.0f, 3.0f});
        
        auto* cam_comp = camera_ent->add_component<CameraComponent>();
        cam_comp->set_fov(60.0f);
        cam_comp->on_init();
        
        // Create bunny
        auto* bunny_ent = scene->create_entity();
        auto* bunny_trans = bunny_ent->add_component<TransformComponent>();
        bunny_trans->transform.set_scale({10.0f, 10.0f, 10.0f});
        
        auto* bunny_mesh = bunny_ent->add_component<MeshRendererComponent>();
        
        ModelProcessSetting setting;
        setting.smooth_normal = true;
        setting.load_materials = false;
        
        std::string model_path = "/Engine/models/bunny.obj";
        auto bunny_model = std::make_shared<Model>(model_path, setting);
        bunny_mesh->set_model(bunny_model);
        bunny_mesh->on_init();
        
        EngineContext::world()->set_active_scene(scene);
        EngineContext::render_system()->get_mesh_manager()->set_active_camera(cam_comp);
        
        // Render with wireframe mode
        EngineContext::render_system()->get_mesh_manager()->set_wireframe(true);
        
        int frames = 0;
        while (frames < 30) {
            Input::get_instance().tick();
            EngineContext::world()->tick(0.016f);
            
            RenderPacket packet;
            packet.active_camera = cam_comp;
            packet.active_scene = scene.get();
            
            if (!EngineContext::render_system()->tick(packet)) {
                break;
            }
            
            frames++;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        
        CHECK(frames > 0);
        
        // Switch back to solid mode
        EngineContext::render_system()->get_mesh_manager()->set_wireframe(false);
        
        frames = 0;
        while (frames < 30) {
            Input::get_instance().tick();
            EngineContext::world()->tick(0.016f);
            
            RenderPacket packet;
            packet.active_camera = cam_comp;
            packet.active_scene = scene.get();
            
            if (!EngineContext::render_system()->tick(packet)) {
                break;
            }
            
            frames++;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        
        CHECK(frames > 0);
        
        test_utils::TestContext::reset();
    }
}
