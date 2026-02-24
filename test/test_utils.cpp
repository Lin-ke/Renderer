#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "test_utils.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/input/input.h"

#include <fstream>
#include <thread>
#include <chrono>

namespace test_utils {

// TestContext static members
bool TestContext::engine_initialized_ = false;
std::string TestContext::test_asset_dir_;

void TestContext::init_engine() {
    if (engine_initialized_) return;
    
    test_asset_dir_ = std::string(ENGINE_PATH) + "/test/test_internal";
    
    std::bitset<8> mode;
    mode.set(EngineContext::StartMode::Asset);
    mode.set(EngineContext::StartMode::Window);
    mode.set(EngineContext::StartMode::Render);
    mode.set(EngineContext::StartMode::SingleThread);
    
    EngineContext::init(mode);
    
    if (auto am = EngineContext::asset()) {
        am->init(test_asset_dir_);
    }
    
    engine_initialized_ = true;
}

void TestContext::shutdown_engine() {
    if (!engine_initialized_) return;
    
    EngineContext::exit();
    engine_initialized_ = false;
}

void TestContext::reset() {
    if (!engine_initialized_) return;
    
    // 1. Clear active scene first
    if (auto world = EngineContext::world()) {
        world->set_active_scene(nullptr);
    }
    
    // 2. Wait for GPU and cleanup render system state
    if (auto rs = EngineContext::render_system()) {
        rs->cleanup_for_test();
    }
    
    // 3. Force cleanup of any remaining entities in world
    if (auto world = EngineContext::world()) {
        // Reset world state completely
        world->set_active_scene(nullptr);
    }
    
    // 4. Process any pending asset operations
    if (auto am = EngineContext::asset()) {
        am->tick();
    }
    
    // 5. Small delay to ensure GPU has finished all work
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

SceneLoadResult SceneLoader::load(const std::string& virtual_path, 
                                   bool set_active) {
    SceneLoadResult result;
    
    auto am = EngineContext::asset();
    if (!am) {
        result.error_msg = "AssetManager is null";
        return result;
    }
    
    // Resolve virtual path to physical path
    auto phys_path_opt = am->get_physical_path(virtual_path);
    if (!phys_path_opt) {
        result.error_msg = "Failed to resolve physical path for: " + virtual_path;
        return result;
    }
    
    std::string phys_path = phys_path_opt->generic_string();
    
    // Check if file exists
    std::ifstream check_file(phys_path);
    if (!check_file.is_open()) {
        result.error_msg = "Scene file does not exist: " + phys_path;
        return result;
    }
    check_file.close();
    
    // Get UID from registered path
    UID scene_uid = am->get_uid_by_path(phys_path);
    
    // Load scene
    auto scene = am->load_asset<Scene>(scene_uid);
    if (!scene) {
        result.error_msg = "Failed to load scene from UID: " + scene_uid.to_string();
        return result;
    }
    
    // Find camera
    CameraComponent* camera = scene->get_camera();
    if (!camera) {
        result.error_msg = "No camera found in loaded scene";
        return result;
    }
    
    // Set as active scene if requested
    if (set_active) {
        EngineContext::world()->set_active_scene(scene);
        if (auto mesh_manager = EngineContext::render_system()->get_mesh_manager()) {
            mesh_manager->set_active_camera(camera);
        }
    }
    
    result.scene = scene;
    result.camera = camera;
    result.success = true;
    return result;
}

bool SceneLoader::scene_exists(const std::string& virtual_path) {
    auto am = EngineContext::asset();
    if (!am) return false;
    
    auto phys_path_opt = am->get_physical_path(virtual_path);
    if (!phys_path_opt) return false;
    
    std::ifstream file(phys_path_opt->generic_string());
    return file.is_open();
}

bool RenderTestApp::capture_screenshot(std::vector<uint8_t>& screenshot_data) {
    auto swapchain = EngineContext::render_system()->get_swapchain();
    if (!swapchain) return false;

    uint32_t current_frame = swapchain->get_current_frame_index();
    RHITextureRef back_buffer = swapchain->get_texture(current_frame);
    if (!back_buffer) return false;

    auto backend = EngineContext::rhi();
    auto pool = backend->create_command_pool({});
    auto context = backend->create_command_context(pool);
    
    context->begin_command();
    context->end_command();
    auto fence = backend->create_fence(false);
    context->execute(fence, nullptr, nullptr);
    fence->wait();
    
    return context->read_texture(back_buffer, screenshot_data.data(), screenshot_data.size());
}

bool RenderTestApp::run(const Config& config, std::vector<uint8_t>& out_screenshot_data, int* out_frames) {
    // 1. Create Scene
    if (config.create_scene_func) {
        if (!config.create_scene_func(config.scene_path)) {
            return false;
        }
    }

    // 2. Load Scene
    auto result = SceneLoader::load(config.scene_path, true);
    if (!result.success) {
        return false;
    }

    if (config.on_scene_loaded_func) {
        config.on_scene_loaded_func(result);
    }

    // 3. Render
    int frames = 0;
    bool screenshot_taken = false;
    
    while (frames < config.max_frames) {
        Input::get_instance().tick();
        EngineContext::world()->tick(0.016f);
        
        RenderPacket packet;
        packet.active_camera = result.camera;
        packet.active_scene = result.scene.get();
        packet.frame_index = frames % 2;
        
        bool should_continue = EngineContext::render_system()->tick(packet);
        if (!should_continue) break;
        
        frames++;
        
        if (config.capture_frame > 0 && frames == config.capture_frame && !screenshot_taken) {
            out_screenshot_data.resize(config.width * config.height * 4);
            if (capture_screenshot(out_screenshot_data)) {
                screenshot_taken = true;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    if (out_frames) {
        *out_frames = frames;
    }
    return screenshot_taken;
}

} // namespace test_utils
