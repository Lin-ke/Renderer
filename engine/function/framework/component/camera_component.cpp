#include "engine/function/framework/component/camera_component.h"
#include "engine/core/math/transform.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/entity.h"
#include "engine/function/input/input.h"
#include "engine/main/engine_context.h"
#include <memory>

void CameraComponent::on_init() {
    // Component::on_init();
    update_matrix();
}

void CameraComponent::on_update(float delta_time) {
    // InitComponentIfNeed();
    if (is_active_camera()) {
        input_move(delta_time);
    }
    update_matrix();
}

void CameraComponent::input_move(float delta_time) {
    auto transform_component = get_owner()->get_component<TransformComponent>();
    if (!transform_component) return;

    float speed = 5.0f;
    float delta = speed * delta_time; // delta_time is in seconds
    float sensitivity = 0.5f;

    Vec3 delta_position = Vec3::Zero();
    
    // Use Input::get_instance()
    const auto& input = Input::get_instance();

    if (input.is_key_down(Key::W)) delta_position += transform_component->transform.front() * delta;
    if (input.is_key_down(Key::S)) delta_position -= transform_component->transform.front() * delta;
    if (input.is_key_down(Key::A)) delta_position -= transform_component->transform.right() * delta;
    if (input.is_key_down(Key::D)) delta_position += transform_component->transform.right() * delta;
    if (input.is_key_down(Key::Space)) delta_position += transform_component->transform.up() * delta;
    if (input.is_key_down(Key::LeftControl)) delta_position -= transform_component->transform.up() * delta;
    
    transform_component->transform.translate(delta_position); // Using Transform directly

    if (input.is_mouse_button_down(MouseButton::Right)) {
        float dx, dy;
        input.get_mouse_delta(dx, dy);
        Vec2 offset(-dx * sensitivity, -dy * sensitivity);

        Vec3 euler_angle = transform_component->transform.get_euler_angle();
        // euler_angle = Math::clamp_euler_angle(euler_angle + Vec3(0.0f, offset.x(), offset.y())); //####TODO####: Math::ClampEulerAngle missing
        euler_angle += Vec3(0.0f, offset.x(), offset.y()); 
        transform_component->transform.set_rotation(euler_angle);

        // FOV
        /* //####TODO####: Scroll input
        fovy_ -= input.get_scroll_delta().y * sensitivity * 2;
        fovy_ = std::clamp(fovy_, 30.0f, 135.0f);
        */
    }
}

bool CameraComponent::is_active_camera() {
    /* //####TODO####: Scene and active camera
    if (get_owner() && get_owner()->get_scene() && 
        get_owner()->get_scene()->get_active_camera().get() == this) {
        return true;
    }
    */
    return true; // Default to true for now if single camera
}

void CameraComponent::update_matrix() {
    auto transform_component = get_owner()->get_component<TransformComponent>();
    if (transform_component) {
        this->position_ = transform_component->transform.get_position();
        this->front_ = transform_component->transform.front();
        this->up_ = transform_component->transform.up();
        this->right_ = transform_component->transform.right();
    }

    prev_view_ = view_;
    prev_proj_ = proj_;

    // TEMP: Simple view matrix looking at origin from position
    view_ = Math::look_at(position_, Vec3(0, 0, 0), Vec3(0, 1, 0));
    proj_ = Math::perspective(Math::to_radians(fovy_), aspect_, near_, far_);
    // proj_(1, 1) *= -1; // Vulkan specific? RD had it. DX11 might differ. Keeping it if project uses Vulkan conventions.

    move_ = (prev_view_ == view_ && prev_proj_ == proj_) ? false : true;

    // frustum_ = create_frustum_from_matrix(proj_ * view_, -1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f); //####TODO####

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
    // EngineContext::render_resource()->set_camera_info(camera_info_); //####TODO####
}
