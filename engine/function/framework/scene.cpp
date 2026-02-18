#include "engine/function/framework/scene.h"
#include "engine/function/framework/component/directional_light_component.h"

DirectionalLightComponent* Scene::get_directional_light() const {
    for (const auto& entity : entities_) {
        if (auto* light = entity->get_component<DirectionalLightComponent>()) {
            return light;
        }
    }
    return nullptr;
}
