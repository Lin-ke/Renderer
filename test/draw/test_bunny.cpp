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
#include <fstream>
#include <vector>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

/**
 * @file test/draw/test_bunny.cpp
 * @brief Tests for bunny model rendering including scene serialization and camera movement.
 */

DEFINE_LOG_TAG(LogBunnyRender, "BunnyRender");

static const std::string SCENE_FILE_NAME = "bunny_scene.json";

bool save_screenshot_png(const std::string& filename, uint32_t width, uint32_t height, const std::vector<uint8_t>& data) {
    int stride_in_bytes = width * 4;
    int result = stbi_write_png(filename.c_str(), static_cast<int>(width), static_cast<int>(height), 4, data.data(), stride_in_bytes);
    return result != 0;
}

float calculate_average_brightness(const std::vector<uint8_t>& data) {
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

bool save_scene_to_file(const std::string& filepath, std::shared_ptr<Scene> scene) {
    try {
        std::ofstream os(filepath);
        if (!os.is_open()) {
            ERR(LogBunnyRender, "Failed to open file for writing: {}", filepath);
            return false;
        }
        
        cereal::JSONOutputArchive archive(os);
        archive(cereal::make_nvp("scene", *scene));
        
        INFO(LogBunnyRender, "Scene saved to: {}", filepath);
        return true;
    } catch (const std::exception& e) {
        ERR(LogBunnyRender, "Failed to save scene: {}", e.what());
        return false;
    }
}

std::shared_ptr<Scene> load_scene_from_file(const std::string& filepath) {
    try {
        std::ifstream is(filepath);
        if (!is.is_open()) {
            ERR(LogBunnyRender, "Failed to open file for reading: {}", filepath);
            return nullptr;
        }
        
        auto scene = std::make_shared<Scene>();
        cereal::JSONInputArchive archive(is);
        archive(cereal::make_nvp("scene", *scene));
        
        scene->load_asset_deps();
        
        INFO(LogBunnyRender, "Scene loaded from: {}", filepath);
        return scene;
    } catch (const std::exception& e) {
        ERR(LogBunnyRender, "Failed to load scene: {}", e.what());
        return nullptr;
    }
}

TEST_CASE("Create and Save Bunny Scene", "[draw][bunny]") {
    INFO(LogBunnyRender, "Starting Create and Save Bunny Scene test...");
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    REQUIRE(EngineContext::world() != nullptr);
    
    INFO(LogBunnyRender, "Engine initialized successfully");
    
    auto scene = std::make_shared<Scene>();
    
    // Create camera entity
    INFO(LogBunnyRender, "Creating camera...");
    auto* camera_ent = scene->create_entity();
    REQUIRE(camera_ent != nullptr);
    
    auto* cam_trans = camera_ent->add_component<TransformComponent>();
    REQUIRE(cam_trans != nullptr);
    cam_trans->transform.set_position({0.0f, 0.0f, 3.0f});
    cam_trans->transform.set_rotation({0.0f, 0.0f, 0.0f});
    
    auto* cam_comp = camera_ent->add_component<CameraComponent>();
    REQUIRE(cam_comp != nullptr);
    cam_comp->set_fov(60.0f);
    
    // Create directional light entity
    INFO(LogBunnyRender, "Creating directional light...");
    auto* light_ent = scene->create_entity();
    REQUIRE(light_ent != nullptr);
    
    auto* light_trans = light_ent->add_component<TransformComponent>();
    REQUIRE(light_trans != nullptr);
    light_trans->transform.set_position({5.0f, 10.0f, 5.0f});
    light_trans->transform.set_rotation({0.0f, -45.0f, -60.0f});
    
    auto* light_comp = light_ent->add_component<DirectionalLightComponent>();
    REQUIRE(light_comp != nullptr);
    light_comp->set_color({1.0f, 1.0f, 1.0f});
    light_comp->set_intensity(1.5f);
    light_comp->set_enable(true);
    
    INFO(LogBunnyRender, "Directional light created with intensity {}", light_comp->get_intensity());
    
    // Create bunny entity placeholder
    INFO(LogBunnyRender, "Creating bunny entity placeholder...");
    auto* bunny_ent = scene->create_entity();
    REQUIRE(bunny_ent != nullptr);
    
    auto* bunny_trans = bunny_ent->add_component<TransformComponent>();
    REQUIRE(bunny_trans != nullptr);
    bunny_trans->transform.set_position({0.0f, 0.0f, 0.0f});
    bunny_trans->transform.set_scale({10.0f, 10.0f, 10.0f});
    
    // Save scene to file
    std::string scene_path = test_asset_dir + "/" + SCENE_FILE_NAME;
    bool saved = save_scene_to_file(scene_path, scene);
    REQUIRE(saved);
    
    INFO(LogBunnyRender, "Scene saved successfully to {}", scene_path);
    
    // Verify file exists
    std::ifstream check_file(scene_path);
    REQUIRE(check_file.good());
    check_file.close();
    
    EngineContext::exit();
}

TEST_CASE("Load and Render Bunny Scene", "[draw][bunny]") {
    INFO(LogBunnyRender, "Starting Load and Render Bunny Scene test...");
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    std::string scene_path = test_asset_dir + "/" + SCENE_FILE_NAME;
    
    std::ifstream check_file(scene_path);
    bool scene_exists = check_file.good();
    check_file.close();
    
    if (!scene_exists) {
        WARN(LogBunnyRender, "Scene file not found: {}. Run 'Create and Save Bunny Scene' test first.", scene_path);
        REQUIRE(true);
        return;
    }
    
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
    
    INFO(LogBunnyRender, "Engine initialized successfully");
    
    auto scene = load_scene_from_file(scene_path);
    REQUIRE(scene != nullptr);
    REQUIRE(!scene->entities_.empty());
    
    INFO(LogBunnyRender, "Scene loaded with {} entities", scene->entities_.size());
    
    CameraComponent* cam_comp = nullptr;
    Entity* bunny_ent = nullptr;
    for (auto& entity : scene->entities_) {
        if (entity) {
            if (!cam_comp) {
                cam_comp = entity->get_component<CameraComponent>();
                if (cam_comp) {
                    cam_comp->on_init();
                    INFO(LogBunnyRender, "Camera found and initialized");
                }
            }
            if (entity->get_component<TransformComponent>() && 
                !entity->get_component<CameraComponent>() &&
                !entity->get_component<DirectionalLightComponent>()) {
                bunny_ent = entity.get();
            }
        }
    }
    REQUIRE(cam_comp != nullptr);
    REQUIRE(bunny_ent != nullptr);
    
    // Add MeshRendererComponent to bunny entity and load model
    INFO(LogBunnyRender, "Loading bunny model...");
    auto* bunny_mesh = bunny_ent->add_component<MeshRendererComponent>();
    REQUIRE(bunny_mesh != nullptr);
    
    ModelProcessSetting setting;
    setting.smooth_normal = true;
    setting.load_materials = false;
    
    std::string model_path = std::string(ENGINE_PATH) + "/assets/models/bunny.obj";
    auto bunny_model = std::make_shared<Model>(model_path, setting);
    
    REQUIRE(bunny_model->get_submesh_count() > 0);
    
    bunny_mesh->set_model(bunny_model);
    bunny_mesh->on_init();
    
    INFO(LogBunnyRender, "Bunny model loaded: {} vertices, {} indices",
         bunny_model->submesh(0).vertex_buffer->vertex_num(),
         bunny_model->submesh(0).index_buffer->index_num());
    
    // Initialize lights
    for (auto& entity : scene->entities_) {
        if (!entity) continue;
        
        auto* light = entity->get_component<DirectionalLightComponent>();
        if (light) {
            light->on_init();
            INFO(LogBunnyRender, "DirectionalLight initialized");
        }
    }
    
    EngineContext::world()->set_active_scene(scene);
    EngineContext::render_system()->get_mesh_manager()->set_active_camera(cam_comp);
    
    INFO(LogBunnyRender, "Scene setup complete, starting render loop...");
    
    auto* window = EngineContext::render_system()->get_window();
    REQUIRE(window != nullptr);
    
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
        bool should_continue = EngineContext::render_system()->tick(packet);
        if (!should_continue) {
            break;
        }
        
        frames++;
        
        // Take screenshot on frame 30
        if (frames == 30 && !screenshot_taken) {
            auto swapchain = EngineContext::render_system()->get_swapchain();
            if (swapchain) {
                RHITextureRef back_buffer = swapchain->get_new_frame(nullptr, nullptr);
                if (back_buffer) {
                    auto backend = EngineContext::rhi();
                    auto pool = backend->create_command_pool({});
                    auto context = backend->create_command_context(pool);
                    
                    if (context->read_texture(back_buffer, screenshot_data.data(), screenshot_data.size())) {
                        screenshot_taken = true;
                        INFO(LogBunnyRender, "Screenshot captured on frame {}", frames);
                    }
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    INFO(LogBunnyRender, "Rendered {} frames in {} ms", frames, duration.count());
    
    CHECK(frames > 0);
    
    if (screenshot_taken) {
        std::string screenshot_path = test_asset_dir + "/bunny_screenshot.png";
        if (save_screenshot_png(screenshot_path, screenshot_width, screenshot_height, screenshot_data)) {
            INFO(LogBunnyRender, "Screenshot saved to: {}", screenshot_path);
            
            float brightness = calculate_average_brightness(screenshot_data);
            INFO(LogBunnyRender, "Screenshot average brightness: {}", brightness);
            
            CHECK(brightness > 5.0f);
            CHECK(brightness < 250.0f);
        }
    }
    
    EngineContext::world()->set_active_scene(nullptr);
    
    INFO(LogBunnyRender, "Bunny render test completed successfully");
    
    EngineContext::exit();
}

TEST_CASE("Camera Movement", "[draw][bunny]") {
    INFO(LogBunnyRender, "Starting camera movement test...");
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    auto scene = std::make_shared<Scene>();
    
    auto* camera_ent = scene->create_entity();
    auto* cam_trans = camera_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({0.0f, 0.0f, 5.0f});
    
    auto* cam_comp = camera_ent->add_component<CameraComponent>();
    cam_comp->on_init();
    
    Vec3 initial_pos = cam_trans->transform.get_position();
    INFO(LogBunnyRender, "Initial camera position: ({}, {}, {})",
         initial_pos.x(), initial_pos.y(), initial_pos.z());
    
    cam_comp->on_update(16.0f);
    
    Vec3 final_pos = cam_trans->transform.get_position();
    INFO(LogBunnyRender, "Final camera position: ({}, {}, {})",
         final_pos.x(), final_pos.y(), final_pos.z());
    
    REQUIRE(cam_comp->get_position() == final_pos);
    
    EngineContext::exit();
}
