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
#include "engine/function/input/input.h"
#include "engine/core/log/Log.h"

#include <thread>
#include <chrono>

DEFINE_LOG_TAG(LogRDGForward, "RDGForward");

/**
 * @file test/draw/test_rdg_forward.cpp
 * @brief Tests for RDG-based forward rendering
 */

TEST_CASE("RDG Forward Pass - Bunny Rendering", "[draw][rdg]") {
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
    
    std::string model_path = std::string(ENGINE_PATH) + "/assets/models/bunny.obj";
    auto bunny_model = std::make_shared<Model>(model_path, setting);
    
    REQUIRE(bunny_model->get_submesh_count() > 0);
    
    bunny_mesh->set_model(bunny_model);
    bunny_mesh->on_init();
    
    auto mesh0 = bunny_model->get_mesh(0);
    if (mesh0) {
        INFO(LogRDGForward, "Bunny loaded: {} vertices, {} indices",
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
    
    EngineContext::world()->set_active_scene(nullptr);
    EngineContext::exit();
}

TEST_CASE("RDG Forward Pass - Wireframe Toggle", "[draw][rdg]") {
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    EngineContext::asset()->init(test_asset_dir);
    
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
    
    std::string model_path = std::string(ENGINE_PATH) + "/assets/models/bunny.obj";
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
    
    EngineContext::world()->set_active_scene(nullptr);
    EngineContext::exit();
}
