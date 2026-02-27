#include "engine/function/framework/component/camera_component.h"
#include "engine/core/math/transform.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/entity.h"
#include "engine/function/input/input.h"
#include "engine/main/engine_context.h"
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

void CameraComponent::input_move(float delta_time) {
    auto transform_component = get_owner()->get_component<TransformComponent>();
    if (!transform_component) return;

    float speed = 20.0f;
    float delta = speed * delta_time;
    float sensitivity = 0.5f;

    Vec3 delta_position = Vec3::Zero();
    const auto& input = Input::get_instance();

    // 使用相机本地坐标系：front（前）、right（右）、up（上）
    // 注意：cross使用右手定则，左手坐标系中 up × front = right
    Vec3 front = transform_component->transform.front();
    Vec3 up = Vec3::UnitY();
    Vec3 right = up.cross(front).normalized();

    if (input.is_key_down(Key::W)) delta_position += front * delta;
    if (input.is_key_down(Key::S)) delta_position -= front * delta;
    if (input.is_key_down(Key::A)) delta_position -= right * delta;
    if (input.is_key_down(Key::D)) delta_position += right * delta;
    if (input.is_key_down(Key::Space)) delta_position += transform_component->transform.up() * delta;
    if (input.is_key_down(Key::LeftControl)) delta_position -= transform_component->transform.up() * delta;
    
    transform_component->transform.translate(delta_position);

    if (input.is_mouse_button_down(MouseButton::Right)) {
        float dx, dy;
        input.get_mouse_delta(dx, dy);
        Vec2 offset(-dx * sensitivity, -dy * sensitivity);

        // Get current euler angles (pitch, yaw, roll)
        Vec3 euler_angle = transform_component->transform.get_euler_angle();
        // Apply rotation: offset.x affects yaw (horizontal), offset.y affects pitch (vertical)
        euler_angle = Math::clamp_euler_angle(euler_angle + Vec3(offset.y, offset.x, 0.0f));
        transform_component->transform.set_rotation(euler_angle);
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
