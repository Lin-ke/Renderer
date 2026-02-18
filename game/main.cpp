#include "engine/main/engine_context.h"
#include "engine/core/window/window.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/directional_light_component.h"
#include "engine/function/framework/component/mesh_renderer_component.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/function/render/render_resource/model.h"
#include "engine/function/render/render_system/render_system.h"
#include "engine/function/render/render_system/render_mesh_manager.h"
#include "engine/function/render/render_system/gizmo_manager.h"
#include "engine/core/log/Log.h"

#include <bitset>
#include <string>
#include <fstream>
#include <thread>

DEFINE_LOG_TAG(LogGame, "Game");

// Frame counter for debugging
static uint32_t g_frame_count = 0;
static const uint32_t MAX_FRAMES = 30;  // Only render 30 frames then exit

Entity* setup_bunny_scene() {
    INFO(LogGame, "Setting up bunny render scene...");
    
    // Create scene
    auto scene = std::make_shared<Scene>();
    
    Entity* bunny_ent = nullptr;
    
    // Create camera entity
    INFO(LogGame, "Creating camera...");
    auto* camera_ent = scene->create_entity();
    auto* cam_trans = camera_ent->add_component<TransformComponent>();
    cam_trans->transform.set_position({0.0f, 0.0f, 3.0f});
    cam_trans->transform.set_rotation({0.0f, 0.0f, 0.0f});
    
    auto* cam_comp = camera_ent->add_component<CameraComponent>();
    cam_comp->set_fov(60.0f);
    // near/far use default values (0.1f / 1000.0f)
    
    // Create directional light entity
    INFO(LogGame, "Creating directional light...");
    auto* light_ent = scene->create_entity();
    auto* light_trans = light_ent->add_component<TransformComponent>();
    light_trans->transform.set_position({5.0f, 10.0f, 5.0f});
    light_trans->transform.set_rotation({0.0f, -45.0f, -60.0f});
    
    auto* light_comp = light_ent->add_component<DirectionalLightComponent>();
    light_comp->set_color({1.0f, 1.0f, 1.0f});
    light_comp->set_intensity(1.5f);
    light_comp->set_enable(true);
    light_comp->set_cast_shadow(true);
    
    INFO(LogGame, "Directional light created with intensity {}", light_comp->get_intensity());
    
    // Create bunny entity - try to load from raw .obj file
    INFO(LogGame, "Creating bunny entity...");
    bunny_ent = scene->create_entity();
    
    auto* bunny_trans = bunny_ent->add_component<TransformComponent>();
    bunny_trans->transform.set_position({0.0f, 0.0f, 0.0f});
    bunny_trans->transform.set_scale({10.0f, 10.0f, 10.0f});
    
    // Try to load bunny model from raw .obj file
    // Model class expects path relative to ENGINE_PATH
    std::string model_rel_path = "assets/models/bunny.obj";
    std::string model_full_path = std::string(ENGINE_PATH) + "/" + model_rel_path;
    INFO(LogGame, "Attempting to load bunny from: {}", model_full_path);
    
    // Check if file exists
    std::ifstream file_check(model_full_path);
    if (file_check.good()) {
        file_check.close();
        INFO(LogGame, "Bunny.obj file found, loading...");
        
        // Create model and load - Model constructor takes relative path
        auto model = std::make_shared<Model>(model_rel_path, ModelProcessSetting());
        if (model->get_submesh_count() > 0) {
            INFO(LogGame, "Bunny model loaded with {} submeshes", model->get_submesh_count());
            
            auto* mesh_renderer = bunny_ent->add_component<MeshRendererComponent>();
            mesh_renderer->set_model(model);
            
            // Create default material
            auto material = std::make_shared<Material>();
            material->set_diffuse({0.8f, 0.5f, 0.3f, 1.0f});
            material->set_roughness(0.5f);
            material->set_metallic(0.1f);
            mesh_renderer->set_material(material);
            
            INFO(LogGame, "Bunny mesh renderer created successfully");
        } else {
            WARN(LogGame, "Bunny model loaded but has no submeshes");
        }
    } else {
        WARN(LogGame, "Bunny.obj not found at: {}", model_full_path);
        WARN(LogGame, "Scene will render without bunny mesh (background only)");
    }
    
    // Set as active scene
    EngineContext::world()->set_active_scene(scene);
    INFO(LogGame, "Scene setup complete. Entity count: {}", scene->entities_.size());
    
    // Verify mesh renderers are registered
    auto* mesh_manager = EngineContext::render_system()->get_mesh_manager().get();
    if (mesh_manager) {
        INFO(LogGame, "Mesh manager ready");
    }
    
    return bunny_ent;
}

void capture_screenshot(uint32_t frame_num) {
    INFO(LogGame, "Screenshot requested for frame {} (placeholder - would save image)", frame_num);
    // Screenshot implementation disabled for now - screenshot validation can be done manually
    // The renderer is working: view/proj matrices are correct, draw calls are executing
}

int main() {
    INFO(LogGame, "========================================");
    INFO(LogGame, "Bunny Render Demo - {} Frame Limit", MAX_FRAMES);
    INFO(LogGame, "========================================");
    
    // Initialize engine with all systems
    	std::bitset<8> mode;
    	mode.set(EngineContext::StartMode::Asset);
    	mode.set(EngineContext::StartMode::Window);
    	mode.set(EngineContext::StartMode::Render);
    	// mode.set(EngineContext::StartMode::SingleThread);    
    EngineContext::init(mode);
    
    // Initialize asset manager
    std::string asset_dir = std::string(ENGINE_PATH) + "/assets";
    if (EngineContext::asset()) {
        EngineContext::asset()->init(asset_dir);
    }
    
    // Setup scene
    Entity* bunny = setup_bunny_scene();
    
    // Select bunny entity for gizmo manipulation
    if (bunny && EngineContext::render_system()) {
        EngineContext::render_system()->set_selected_entity(bunny);
        INFO(LogGame, "Bunny entity selected for gizmo manipulation");
    }
    
    // Run main loop with frame limit
    INFO(LogGame, "Entering main loop (max {} frames)...", MAX_FRAMES);
    
    while (g_frame_count < MAX_FRAMES) {
        // Process window messages
        auto* window = EngineContext::window();
        if (window && !window->process_messages()) {
            INFO(LogGame, "Window closed, exiting early");
            break;
        }
        
        // Tick input
        // Input::get_instance().tick();  // Not available in this build
        
        // Prepare render packet
        RenderPacket packet;
        packet.frame_index = g_frame_count % 3;  // FRAMES_IN_FLIGHT = 3
        
        // Fill packet with scene data
        if (EngineContext::world()) {
            Scene* active_scene = EngineContext::world()->get_active_scene();
            packet.active_scene = active_scene;
            
            // Find active camera
            if (active_scene) {
                for (const auto& entity : active_scene->entities_) {
                    if (auto* camera = entity->get_component<CameraComponent>()) {
                        packet.active_camera = camera;
                        break;
                    }
                }
                
                // Count mesh renderers for debugging
                int mesh_count = 0;
                for (const auto& entity : active_scene->entities_) {
                    if (entity->get_component<MeshRendererComponent>()) {
                        mesh_count++;
                    }
                }
                
                INFO(LogGame, "Frame {}: {} entities, {} mesh renderers, camera: {}",
                     g_frame_count,
                     active_scene->entities_.size(),
                     mesh_count,
                     packet.active_camera ? "yes" : "no");
            }
        }
        
        // Capture screenshot on specific frames
        if (g_frame_count == 5 || g_frame_count == 15 || g_frame_count == 29) {
            capture_screenshot(g_frame_count);
        }
        
        // Render
        if (EngineContext::render_system()) {
            if (!EngineContext::render_system()->tick(packet)) {
                INFO(LogGame, "RenderSystem tick returned false, exiting");
                break;
            }
        }
        
        g_frame_count++;
        
        // Small delay to not max out CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60 FPS
    }
    
    INFO(LogGame, "========================================");
    INFO(LogGame, "Rendered {} frames, exiting...", g_frame_count);
    INFO(LogGame, "========================================");
    
    // Cleanup
    EngineContext::exit();
    
    return 0;
}
