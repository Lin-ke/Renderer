#include "engine/function/framework/component/volume_light_component.h"
#include "engine/core/math/math.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/entity.h"
#include "engine/main/engine_context.h"

VolumeLightComponent::~VolumeLightComponent() {
}

void VolumeLightComponent::on_init() {
}

void VolumeLightComponent::on_update(float delta_time) {
    for (int i = 0; i < 2; i++) {
        if (update_frequencies_[i] != 0) {
            update_cnts_[i]++;
            if (update_cnts_[i] >= update_frequencies_[i]) {
                update_cnts_[i] = 0;
                should_update_[i] = true;
            } else {
                should_update_[i] = false;
            }
        } else {
            should_update_[i] = false;
        }
    }
}

void VolumeLightComponent::update_light_info() {
    auto transform = get_owner()->get_component<TransformComponent>();
    if (!transform) return;

    Vec3 extent = { (probe_counts_.x() - 1) * grid_step_.x(),
                    (probe_counts_.y() - 1) * grid_step_.y(),
                    (probe_counts_.z() - 1) * grid_step_.z() };
    box_ = BoundingBox(transform->transform.get_position() - extent / 2.0f, transform->transform.get_position() + extent / 2.0f);

    info_.setting.grid_start_position = box_.min;
    info_.setting.grid_step = grid_step_;
    info_.setting.probe_counts = probe_counts_;
    info_.setting.bounding_box = box_;

}

void VolumeLightComponent::update_texture() {
}
