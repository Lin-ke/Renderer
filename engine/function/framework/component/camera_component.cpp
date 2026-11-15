#include "engine/function/framework/component/camera_component.h"
#include "engine/core/math/transform.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/entity.h"
#include "engine/function/input/input.h"
#include "engine/main/engine_context.h"
#include <algorithm>
#include <cmath>
#include <memory>

void CameraComponent::on_init() {
    update_matrix();
}

void CameraComponent::on_update(float delta_time) {
    if (is_active_camera() && !external_control_) {
        input_move(delta_time);
    }
    update_matrix();
}

void CameraComponent::set_external_control(bool enabled) {
    external_control_ = enabled;
    if (enabled) {
        input_velocity_ = Vec3::Zero();
        pending_rotation_ = Vec3::Zero();
    }
}

void CameraComponent::set_input_smoothing(float seconds) {
    input_smoothing_seconds_ = std::max(0.0f, seconds);
    input_velocity_ = Vec3::Zero();
    pending_rotation_ = Vec3::Zero();
}

void CameraComponent::input_move(float delta_time) {
    auto transform_component = get_owner()->get_component<TransformComponent>();
    if (!transform_component) return;

    constexpr float speed = 20.0f;
    constexpr float sensitivity = 0.5f;
    const bool smooth_input = input_smoothing_seconds_ > 0.0f;
    const float blend = smooth_input ? -std::expm1(-delta_time / input_smoothing_seconds_) : 1.0f;

    Vec3 target_velocity = Vec3::Zero();
    const auto& input = Input::get_instance();

    // 使用相机本地坐标系：front（前）、right（右）、up（上）
    // 注意：cross使用右手定则，左手坐标系中 up × front = right
    Vec3 front = transform_component->transform.front();
    Vec3 up = Vec3::UnitY();
    Vec3 right = up.cross(front).normalized();

    if (input.is_key_down(Key::W)) target_velocity += front * speed;
    if (input.is_key_down(Key::S)) target_velocity -= front * speed;
    if (input.is_key_down(Key::A)) target_velocity -= right * speed;
    if (input.is_key_down(Key::D)) target_velocity += right * speed;
    if (input.is_key_down(Key::Space)) target_velocity += transform_component->transform.up() * speed;
    if (input.is_key_down(Key::LeftControl)) target_velocity -= transform_component->transform.up() * speed;

    Vec3 delta_position = target_velocity * delta_time;
    if (smooth_input) {
        const Vec3 velocity_difference = input_velocity_ - target_velocity;
        // Integrate the exponential velocity change across the entire frame.
        delta_position += velocity_difference * (input_smoothing_seconds_ * blend);
        input_velocity_ -= velocity_difference * blend;
        if (input_velocity_.squared_length() < 1e-6f) input_velocity_ = Vec3::Zero();
    }
    transform_component->transform.translate(delta_position);

    Vec3 rotation_delta = Vec3::Zero();
    const bool mouse_look = input.is_mouse_button_down(MouseButton::Right);
    if (mouse_look) {
        float dx, dy;
        input.get_mouse_delta(dx, dy);
        rotation_delta = {-dy * sensitivity, -dx * sensitivity, 0.0f};
    }

    const Vec3 euler_angle = transform_component->transform.get_euler_angle();
    if (smooth_input) {
        pending_rotation_ += rotation_delta;
        if (pending_rotation_.squared_length() == 0.0f) return;
        // Keep the target within the pitch limit so reversing away from it responds immediately.
        pending_rotation_.x = std::clamp(euler_angle.x + pending_rotation_.x, -89.9f, 89.9f) - euler_angle.x;
        rotation_delta = pending_rotation_.squared_length() < 1e-6f ? pending_rotation_ : pending_rotation_ * blend;
        pending_rotation_ -= rotation_delta;
    }
    if (rotation_delta.x != 0.0f || rotation_delta.y != 0.0f ||
        (!smooth_input && mouse_look)) {
        transform_component->transform.set_rotation(Math::clamp_euler_angle(euler_angle + rotation_delta));
    }
}

bool CameraComponent::is_active_camera() {
    return true;
}

void CameraComponent::update_matrix() {
    auto transform_component = get_owner()->get_component<TransformComponent>();
    if (transform_component) {
        // Use world-space transforms to support hierarchy
        this->position_ = transform_component->get_world_position();
        
        // Extract world-space orientation vectors from world rotation
        Quaternion world_rot = transform_component->get_world_rotation();
        this->front_ = world_rot.rotate_vector(Vec3::UnitZ());
        this->up_ = world_rot.rotate_vector(Vec3::UnitY());
        this->right_ = world_rot.rotate_vector(Vec3::UnitX());
    }

    prev_view_ = view_;
    prev_proj_ = proj_;

    // Use world up (UnitY) for look_at to prevent roll when pitching
    // The front direction already contains the correct orientation from transform
    view_ = Math::look_at(position_, position_ + front_, Vec3::UnitY());
    proj_ = Math::perspective(Math::to_radians(fovy_), aspect_, near_, far_);


    move_ = (prev_view_ == view_ && prev_proj_ == proj_) ? false : true;



    camera_info_.view = view_;
    camera_info_.proj = proj_;
    camera_info_.prev_view = prev_view_;
    camera_info_.prev_proj = prev_proj_;
    camera_info_.inv_view = get_inv_view_matrix();
    camera_info_.inv_proj = get_inv_projection_matrix();
    camera_info_.pos = position_;
    camera_info_.front = front_;
    camera_info_.up = up_;
    camera_info_.right = right_;
    camera_info_.near_plane = near_;
    camera_info_.far_plane = far_;
    camera_info_.fov = Math::to_radians(fovy_);
    camera_info_.aspect = aspect_;
    camera_info_.frustum = frustum_;
}

void CameraComponent::update_camera_info() {
}

REGISTER_CLASS_IMPL(CameraComponent)
