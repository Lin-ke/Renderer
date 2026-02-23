#include "engine/function/framework/scene.h"
#include "engine/function/framework/component/camera_component.h"
#include "engine/function/framework/component/directional_light_component.h"

CameraComponent* Scene::get_camera() const {
    for (const auto& entity : entities_) {
        if (!entity) continue;
        if (auto* cam = entity->get_component<CameraComponent>()) {
            return cam;
        }
    }
    return nullptr;
}

DirectionalLightComponent* Scene::get_directional_light() const {
    for (const auto& entity : entities_) {
        if (auto* light = entity->get_component<DirectionalLightComponent>()) {
            return light;
        }
    }
    return nullptr;
}
