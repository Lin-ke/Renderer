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

static const std::string SCENE_FILE_NAME = "bunny_scene.asset";

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
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    EngineContext::asset()->init(test_asset_dir);
    
    REQUIRE(EngineContext::world() != nullptr);
    
    auto scene = std::make_shared<Scene>();
    
    // Create camera entity
    // Note: World Space is +X Forward, +Y Up, +Z Right
    auto* camera_ent = scene->create_entity();
    auto* cam_trans = camera_ent->add_component<TransformComponent>();
    // Place camera at (-3, 0, 0) so it's looking toward origin (where bunny is)
    // With default rotation, front is +X, so at (-3, 0, 0) camera looks toward +X (toward origin)
    cam_trans->transform.set_position({-3.0f, 0.0f, 0.0f});
    
    auto* cam_comp = camera_ent->add_component<CameraComponent>();
    cam_comp->set_fov(60.0f);
    
    // Create directional light entity
    auto* light_ent = scene->create_entity();
    auto* light_trans = light_ent->add_component<TransformComponent>();
    light_trans->transform.set_position({5.0f, 10.0f, 5.0f});
    light_trans->transform.set_rotation({0.0f, -45.0f, -60.0f});
    
    auto* light_comp = light_ent->add_component<DirectionalLightComponent>();
    light_comp->set_color({1.0f, 1.0f, 1.0f});
    light_comp->set_intensity(1.5f);
    
    // Create bunny entity
    auto* bunny_ent = scene->create_entity();
    auto* bunny_trans = bunny_ent->add_component<TransformComponent>();
    bunny_trans->transform.set_scale({10.0f, 10.0f, 10.0f});

    // Add MeshRendererComponent and load model as an asset
    auto* bunny_renderer = bunny_ent->add_component<MeshRendererComponent>();
    
    // Load raw model file with explicit UID for consistent testing
    std::string model_raw_path = "assets/models/bunny.obj";
    UID model_uid = UID::from_hash("/Game/models/bunny.binasset");
    auto bunny_model = Model::Load(model_raw_path, true, true, false, model_uid);
    REQUIRE(bunny_model != nullptr);
    
    // Mark as dirty to force save (Load() clears dirty flag)
    bunny_model->mark_dirty();
    
    // Save model as a proper engine asset
    EngineContext::asset()->save_asset(bunny_model, "/Game/models/bunny.binasset");
    
    // Assign to renderer
    bunny_renderer->set_model(bunny_model);
    
    // Create and assign a material asset with PBR properties
    auto bunny_mat = std::make_shared<Material>();
    bunny_mat->set_diffuse({0.8f, 0.5f, 0.3f, 1.0f});
    bunny_mat->set_roughness(0.2f);
    bunny_mat->set_metallic(0.8f);
    
    // Set deterministic UID for material
    UID material_uid = UID::from_hash("/Game/materials/bunny_mat.asset");
    bunny_mat->set_uid(material_uid);
    
    // Save material as an asset
    EngineContext::asset()->save_asset(bunny_mat, "/Game/materials/bunny_mat.asset");
    bunny_renderer->set_material(bunny_mat);
    
    // Assign a deterministic UID to the scene
    UID scene_uid = UID::from_hash("/Game/bunny_scene.asset");
    scene->set_uid(scene_uid);
    
    // Save scene using AssetManager to ensure all dependencies are correctly serialized
    EngineContext::asset()->save_asset(scene, "/Game/" + SCENE_FILE_NAME);
    
    // Clean up auto-generated UUID-named dependency files
    // Keep only human-readable named files (bunny_scene.asset, bunny.binasset, bunny_mat.asset)
    size_t cleaned = test_utils::cleanup_uuid_named_assets(test_asset_dir + "/assets");
    if (cleaned > 0) {
        INFO(LogBunnyRender, "Cleaned up {} auto-generated UUID-named asset files", cleaned);
    }
    
    EngineContext::exit();
}

TEST_CASE("Load and Render Bunny Scene", "[draw][bunny]") {
    std::string test_asset_dir = std::string(ENGINE_PATH) + "/test/test_internal";
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    // Set up asset manager correctly
    EngineContext::asset()->init(test_asset_dir);
    
    // Register the scene file so it can be resolved by virtual path
    // Note: AssetManager::init appends /assets to the path
    std::string scene_phys_path = test_asset_dir + "/assets/" + SCENE_FILE_NAME;
    UID scene_uid = UID::from_hash("/Game/bunny_scene.asset");
    EngineContext::asset()->register_path(scene_uid, scene_phys_path);
    
    // Note: Dependencies (Model, Material) are automatically discovered by scan_directory()
    // during AssetManager::init(). The asset files (bunny.binasset, bunny_mat.asset) contain
    // their UIDs in the header, which are registered automatically.
    
    // Register component classes
    TransformComponent::register_class();
    CameraComponent::register_class();
    MeshRendererComponent::register_class();
    DirectionalLightComponent::register_class();
    
    REQUIRE(EngineContext::rhi() != nullptr);
    REQUIRE(EngineContext::render_system() != nullptr);
    
    // Load scene via AssetManager - this automatically resolves all dependencies (Model, Material, etc.)
    auto scene = EngineContext::asset()->load_asset<Scene>(scene_uid);
    REQUIRE(scene != nullptr);
    REQUIRE(!scene->entities_.empty());
    
    CameraComponent* cam_comp = nullptr;
    MeshRendererComponent* bunny_renderer = nullptr;
    
    // Find required components and initialize them
    // Note: Components loaded from deserialization don't have on_init() called automatically,
    // so we need to call it manually
    for (auto& entity : scene->entities_) {
        if (!entity) continue;
        
        // Initialize all components (not just the ones we need)
        // Note: Components loaded from deserialization don't have on_init() called automatically
        for (auto& comp : entity->get_components()) {
            if (comp) {
                comp->on_init();
            }
        }
        
        if (auto* cam = entity->get_component<CameraComponent>()) {
            cam_comp = cam;
        }
        
        if (auto* renderer = entity->get_component<MeshRendererComponent>()) {
            bunny_renderer = renderer;
        }
    }
    
    REQUIRE(cam_comp != nullptr);
    REQUIRE(bunny_renderer != nullptr);
    
    auto bunny_model = bunny_renderer->get_model();
    REQUIRE(bunny_model != nullptr);
    REQUIRE(bunny_model->get_submesh_count() > 0);
    
    // Verify material is loaded
    auto bunny_mat = bunny_renderer->get_material(0);
    CHECK(bunny_mat != nullptr);
    
    INFO(LogBunnyRender, "Bunny automatically loaded as asset: {} vertices",
         bunny_model->submesh(0).vertex_buffer->vertex_num());
    
    EngineContext::world()->set_active_scene(scene);
    EngineContext::render_system()->get_mesh_manager()->set_active_camera(cam_comp);
    
    // Debug: Check batch collection through mesh manager
    std::vector<render::DrawBatch> manager_batches;
    for (auto& entity : scene->entities_) {
        if (!entity) continue;
        if (auto* renderer = entity->get_component<MeshRendererComponent>()) {
            renderer->collect_draw_batch(manager_batches);
        }
    }
    INFO(LogBunnyRender, "Total batches from scene: {}", manager_batches.size());
    
    int frames = 0;
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
        
        bool should_continue = EngineContext::render_system()->tick(packet);
        if (!should_continue) break;
        
        frames++;
        
        if (frames == 30 && !screenshot_taken) {
            auto swapchain = EngineContext::render_system()->get_swapchain();
            if (swapchain) {
                uint32_t current_frame = swapchain->get_current_frame_index();
                RHITextureRef back_buffer = swapchain->get_texture(current_frame);
                if (back_buffer) {
                    auto backend = EngineContext::rhi();
                    auto pool = backend->create_command_pool({});
                    auto context = backend->create_command_context(pool);
                    
                    context->begin_command(); context->end_command();
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
    
    if (screenshot_taken) {
        std::string screenshot_path = test_asset_dir + "/bunny_asset_screenshot.png";
        if (save_screenshot_png(screenshot_path, screenshot_width, screenshot_height, screenshot_data)) {
            float brightness = calculate_average_brightness(screenshot_data);
            INFO(LogBunnyRender, "Screenshot saved: {} (brightness: {:.1f})", screenshot_path, brightness);
            CHECK(brightness > 1.0f);
        }
    }
    
    EngineContext::world()->set_active_scene(nullptr);
    EngineContext::exit();
}

TEST_CASE("Camera Movement", "[draw][bunny]") {
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
    cam_comp->on_update(16.0f);
    Vec3 final_pos = cam_trans->transform.get_position();
    
    REQUIRE(cam_comp->get_position() == final_pos);
    
    EngineContext::exit();
}
