#include "engine/function/framework/component/directional_light_component.h"
#include "engine/core/math/math.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/entity.h"

#include "engine/main/engine_context.h"
#include "engine/core/reflect/class_db.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <cmath>


void DirectionalLightComponent::on_init() {
    for (int i = 0; i < 4; i++) update_cnts_[i] = update_frequencies_[i];
}

void DirectionalLightComponent::on_update(float delta_time) {
    for (int i = 0; i < DIRECTIONAL_SHADOW_CASCADE_LEVEL; i++) {
        update_cnts_[i]++;
        if (update_cnts_[i] >= update_frequencies_[i]) update_cnts_[i] = 0;
    }
}

void DirectionalLightComponent::update_matrix() {
    auto transform = get_owner()->get_component<TransformComponent>();
    if (transform) {
        front_ = transform->transform.front();
        up_ = transform->transform.up();
    } else {
        front_ = Vec3::UnitX();
        up_ = Vec3::UnitY();
    }
}

void DirectionalLightComponent::update_cascades() {
    //####TODO####: Camera Component and Scene
}

void DirectionalLightComponent::update_light_info() {
    update_matrix();
    update_cascades();
}

REGISTER_CLASS_IMPL(DirectionalLightComponent)
