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

#include <fstream>

namespace test_utils {

SceneLoadResult SceneLoader::load(const std::string& virtual_path, 
                                   bool should_init_components,
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
    
    // Initialize components if requested
    if (should_init_components) {
        SceneLoader::init_components(scene.get());
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

void SceneLoader::init_components(Scene* scene) {
    if (!scene) return;
    
    for (auto& entity : scene->entities_) {
        if (!entity) continue;
        for (auto& comp : entity->get_components()) {
            if (comp) {
                comp->on_init();
            }
        }
    }
}

bool SceneLoader::scene_exists(const std::string& virtual_path) {
    auto am = EngineContext::asset();
    if (!am) return false;
    
    auto phys_path_opt = am->get_physical_path(virtual_path);
    if (!phys_path_opt) return false;
    
    std::ifstream file(phys_path_opt->generic_string());
    return file.is_open();
}

} // namespace test_utils
