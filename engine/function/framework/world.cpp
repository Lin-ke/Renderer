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
    active_scene_ = scene;
    INFO(LogWorld, "Active scene set, entity count: {}", 
         scene ? scene->entities_.size() : 0);
}

void World::tick(float delta_time) {
    if (!active_scene_) return;
    active_scene_->tick(delta_time);
}

std::vector<MeshRendererComponent*> World::get_mesh_renderers() const {
    std::vector<MeshRendererComponent*> renderers;
    if (!active_scene_) return renderers;

    for (auto& entity : active_scene_->entities_) {
        if (!entity) continue;
        auto* mesh = entity->get_component<MeshRendererComponent>();
        if (mesh) {
            renderers.push_back(mesh);
        }
    }
    return renderers;
}

CameraComponent* World::get_active_camera() const {
    if (!active_scene_) return nullptr;

    for (auto& entity : active_scene_->entities_) {
        if (!entity) continue;
        auto* camera = entity->get_component<CameraComponent>();
        if (camera) {
            return camera;
        }
    }
    return nullptr;
}
