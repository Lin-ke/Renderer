#include "engine/function/framework/world.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"

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

void World::set_active_scene(std::shared_ptr<Scene> scene) {
    if (active_scene_ == scene) {
        return;
    }
    active_scene_ = scene;
    INFO(LogWorld, "Active scene set, entity count: {}", 
         scene ? scene->entities_.size() : 0);
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
