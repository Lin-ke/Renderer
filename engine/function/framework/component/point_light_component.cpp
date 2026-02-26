#include "engine/function/framework/component/point_light_component.h"
#include "engine/core/math/math.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/entity.h"
#include "engine/main/engine_context.h"
#include "engine/core/reflect/class_db.h"

REGISTER_CLASS_IMPL(PointLightComponent)

PointLightComponent::~PointLightComponent() {
    /* //####TODO####: RenderResource logic
    if (!EngineContext::destroyed() && point_light_id_ != 0) {
        // EngineContext::render_resource()->release_point_light_id(point_light_id_);
    }
    */
}

void PointLightComponent::on_init() {
    // Component::on_init();
    // point_light_id_ = EngineContext::render_resource()->allocate_point_light_id(); //####TODO####
}

void PointLightComponent::on_update(float delta_time) {
    // InitComponentIfNeed();
}

void PointLightComponent::update_light_info() {
    Vec3 pos = Vec3::Zero();
    auto transform = get_owner()->get_component<TransformComponent>();
    if (transform) {
        pos = transform->get_world_position();
        sphere_ = BoundingSphere(pos, far_);
    }

    info_.pos = pos;
    info_.color = color_;
    info_.intensity = intensity_;
    info_.fog_scattering = fog_scattering_;
    info_.near_plane = near_; // Renamed in struct? check RenderStructs.h: near_plane
    info_.far_plane = far_;
    info_.bias = constant_bias_;
    info_.enable = enable_;
    info_.sphere = sphere_;
    info_.shadow_id = point_shadow_id_;

    if (point_shadow_id_ < MAX_POINT_SHADOW_COUNT) {
        info_.c1 = evsm_[0];
        info_.c2 = evsm_[1];

        info_.view[0] = Math::look_at(info_.pos, info_.pos + Vec3::UnitX(), -Vec3::UnitY());
        info_.view[1] = Math::look_at(info_.pos, info_.pos - Vec3::UnitX(), -Vec3::UnitY());
        info_.view[2] = Math::look_at(info_.pos, info_.pos + Vec3::UnitY(), Vec3::UnitZ());
        info_.view[3] = Math::look_at(info_.pos, info_.pos - Vec3::UnitY(), -Vec3::UnitZ());
        info_.view[4] = Math::look_at(info_.pos, info_.pos + Vec3::UnitZ(), -Vec3::UnitY());
        info_.view[5] = Math::look_at(info_.pos, info_.pos - Vec3::UnitZ(), -Vec3::UnitY());

        info_.proj = Math::perspective(Math::to_radians(90.0f), 1.0f, near_, far_);
    }

    // EngineContext::render_resource()->set_point_light_info(info_, point_light_id_); //####TODO####
}
