#include "engine/function/framework/component/directional_light_component.h"
#include "engine/core/math/math.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/entity.h"
// #include "engine/function/framework/component/camera_component.h" //####TODO####
// #include "engine/function/framework/scene/scene.h" //####TODO####
#include "engine/main/engine_context.h"
#include "engine/core/reflect/class_db.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <cmath>

REGISTER_CLASS_IMPL(DirectionalLightComponent)

void DirectionalLightComponent::on_init() {
    // Component::on_init();
    for (int i = 0; i < 4; i++) update_cnts_[i] = update_frequencies_[i];
}

void DirectionalLightComponent::on_update(float delta_time) {
    // InitComponentIfNeed(); //####TODO####

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
    /* //####TODO####: Camera Component and Scene
    auto camera = get_owner()->get_scene()->get_active_camera(); 
    if(!camera) return;

    std::array<float, DIRECTIONAL_SHADOW_CASCADE_LEVEL> cascade_splits;

    float near_clip = camera->get_near();
    float far_clip = camera->get_far();
    float clip_range = far_clip - near_clip;

    float min_z = near_clip;
    float max_z = near_clip + clip_range;

    float range = max_z - min_z;
    float ratio = max_z / min_z;

    for (int i = 0; i < DIRECTIONAL_SHADOW_CASCADE_LEVEL; i++) {
        float p = (i + 1) / (float)(DIRECTIONAL_SHADOW_CASCADE_LEVEL);
        float log = min_z * std::pow(ratio, p);
        float uniform = min_z + range * p;
        float d = cascade_split_lambda_ * (log - uniform) + uniform;
        cascade_splits[i] = (d - near_clip) / clip_range;
    }

    float last_split_dist = 0.0;
    for (int i = 0; i < DIRECTIONAL_SHADOW_CASCADE_LEVEL; i++) {
        float split_dist = cascade_splits[i];

        Vec3 frustum_corners[8] = {
            Vec3(-1.0f,  1.0f, 0.0f), Vec3(1.0f,  1.0f, 0.0f),
            Vec3(1.0f, -1.0f, 0.0f), Vec3(-1.0f, -1.0f, 0.0f),
            Vec3(-1.0f,  1.0f,  1.0f), Vec3(1.0f,  1.0f,  1.0f),
            Vec3(1.0f, -1.0f,  1.0f), Vec3(-1.0f, -1.0f,  1.0f),
        };

        Mat4 inv_cam = (camera->get_projection_matrix() * camera->get_view_matrix()).inverse();
        for (uint32_t j = 0; j < 8; j++) {
            Vec4 inv_corner = inv_cam * Vec4(frustum_corners[j].x(), frustum_corners[j].y(), frustum_corners[j].z(), 1.0f);
            frustum_corners[j] = Vec3(inv_corner.x(), inv_corner.y(), inv_corner.z()) / inv_corner.w();
        }

        for (uint32_t j = 0; j < 4; j++) {
            Vec3 dist = frustum_corners[j + 4] - frustum_corners[j];
            frustum_corners[j + 4] = frustum_corners[j] + (dist * split_dist);
            frustum_corners[j] = frustum_corners[j] + (dist * last_split_dist);
        }

        Vec3 frustum_center = Vec3::Zero();
        for (uint32_t j = 0; j < 8; j++) frustum_center += frustum_corners[j];
        frustum_center /= 8.0f;

        float radius = 0.0f;
        for (uint32_t j = 0; j < 8; j++) {
            float distance = (frustum_corners[j] - frustum_center).norm();
            radius = std::max(radius, distance);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        Vec3 max_extents = Vec3::Constant(radius);
        Vec3 min_extents = -max_extents;

        Vec3 light_dir = front_.normalized();
        Mat4 light_view_matrix = Math::look_at(frustum_center - light_dir * radius, frustum_center, Vec3(0.0f, 1.0f, 0.0f));
        Mat4 light_ortho_matrix = Math::ortho(min_extents.x(), max_extents.x(), min_extents.y(), max_extents.y(), 0.0f, max_extents.z() - min_extents.z());
        light_ortho_matrix(1, 1) *= -1;

        if (update_cnts_[i] == 0) {
            light_infos_[i].depth = (camera->get_near() + split_dist * clip_range);
            light_infos_[i].view = light_view_matrix;
            light_infos_[i].proj = light_ortho_matrix;
            auto transform = get_owner()->get_component<TransformComponent>();
            if (transform) light_infos_[i].pos = transform->get_position();
            
            light_infos_[i].color = color_;
            light_infos_[i].intensity = intensity_;
            light_infos_[i].fog_scattering = fog_scattering_;
            light_infos_[i].cast_shadow = cast_shadow_;
            light_infos_[i].dir = light_dir;

            // light_infos_[i].frustum = create_frustum_from_matrix(light_ortho_matrix * light_view_matrix, -1.0, 1.0, -1.0, 1.0, 0.0, 1.0); //####TODO####
            light_infos_[i].sphere = BoundingSphere(frustum_center, radius);

            // EngineContext::render_resource()->set_directional_light_info(light_infos_[i], i); //####TODO####
        }
        last_split_dist = cascade_splits[i];
    }
    */
}

void DirectionalLightComponent::update_light_info() {
    update_matrix();
    update_cascades();
}
