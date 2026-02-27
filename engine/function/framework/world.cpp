#include "engine/function/framework/world.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/main/engine_context.h"

DEFINE_LOG_TAG(LogWorld, "World");

std::unique_ptr<World> World::instance_;

World::World() = default;

World::~World() {
    destroy();
}

void World::init() {
    if (initialized_) return;
    initialized_ = true;
    // INFO(LogWorld, "World initialized");
}

void World::destroy() {
    if (!initialized_) return;
    active_scene_.reset();
    initialized_ = false;
    // INFO(LogWorld, "World destroyed");
}

World& World::get() {
    if (!instance_) {
        instance_ = std::make_unique<World>();
    }
    return *instance_;
}

void World::set_active_scene(std::shared_ptr<Scene> scene, const std::string& virtual_path) {
    if (active_scene_ == scene) {
        return;
    }
    active_scene_ = scene;
    if (scene) {
        active_scene_virtual_path_ = virtual_path;
        scene->set_virtual_path(virtual_path);
    } else {
        active_scene_virtual_path_.clear();
    }
    INFO(LogWorld, "Active scene set, entity count: {}", 
         scene ? scene->entities_.size() : 0);
}

bool World::save_active_scene() {
    if (!active_scene_) {
        WARN(LogWorld, "Cannot save scene: no active scene");
        return false;
    }
    
    auto* asset_mgr = EngineContext::asset();
    if (!asset_mgr) {
        ERR(LogWorld, "Cannot save scene: AssetManager not available");
        return false;
    }
    
    // Determine save path
    std::string save_path = active_scene_virtual_path_;
    if (save_path.empty()) {
        // Generate default path in /Game/scene/
        std::string scene_name = "scene_" + active_scene_->get_uid().to_string();
        save_path = "/Game/scene/" + scene_name + ".asset";
        INFO(LogWorld, "No virtual path recorded, using default: {}", save_path);
    }
    
    // Ensure the scene has a UID
    if (active_scene_->get_uid().is_empty()) {
        active_scene_->set_uid(UID::generate());
    }
    
    // Mark scene as dirty to ensure it's saved
    active_scene_->mark_dirty();
    
    // Save the scene
    asset_mgr->save_asset(active_scene_, save_path);
    
    // Update the virtual path if it was a new default path
    if (active_scene_virtual_path_.empty()) {
        active_scene_virtual_path_ = save_path;
        active_scene_->set_virtual_path(save_path);
    }
    
    INFO(LogWorld, "Scene saved to: {}", save_path);
    return true;
}

void World::tick(float delta_time) {
    if (!active_scene_) return;
    active_scene_->tick(delta_time);
}

static void collect_mesh_renderers_recursive(Entity* entity,
                                              std::vector<MeshRendererComponent*>& out) {
    if (!entity) return;
    auto* mesh = entity->get_component<MeshRendererComponent>();
    if (mesh) {
        out.push_back(mesh);
    }
    for (auto& child : entity->get_children()) {
        collect_mesh_renderers_recursive(child.get(), out);
    }
}

static Entity* find_camera_recursive(Entity* entity) {
    if (!entity) return nullptr;
    if (entity->get_component<CameraComponent>()) return entity;
    for (auto& child : entity->get_children()) {
        auto* found = find_camera_recursive(child.get());
        if (found) return found;
    }
    return nullptr;
}

std::vector<MeshRendererComponent*> World::get_mesh_renderers() const {
    std::vector<MeshRendererComponent*> renderers;
    if (!active_scene_) return renderers;

    for (auto& entity : active_scene_->entities_) {
        collect_mesh_renderers_recursive(entity.get(), renderers);
    }
    return renderers;
}

CameraComponent* World::get_active_camera() const {
    if (!active_scene_) return nullptr;

    for (auto& entity : active_scene_->entities_) {
        if (!entity) continue;
        auto* found = find_camera_recursive(entity.get());
        if (found) {
            return found->get_component<CameraComponent>();
        }
    }
    return nullptr;
}
