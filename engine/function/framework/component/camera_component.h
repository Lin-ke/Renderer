#pragma once

#include "engine/function/framework/component.h"
#include "engine/core/math/math.h"
#include "engine/function/render/data/render_structs.h"
#include "engine/core/reflect/class_db.h"

class CameraComponent : public Component {
    CLASS_DEF(CameraComponent, Component)
public:
    CameraComponent() = default;
    virtual ~CameraComponent() = default;

    virtual void on_init();
    virtual void on_update(float delta_time);

    bool is_active_camera();
    bool is_moved() const { return move_; }

    void set_fov(float fovy) { this->fovy_ = fovy; }

    inline Vec3 get_position() const { return position_; }
    inline Vec3 get_front() const { return front_; }
    inline Vec3 get_up() const { return right_.cross(front_).normalized(); }
    inline Vec3 get_right() const { return right_; }
    inline float get_near() const { return near_; }
    inline float get_far() const { return far_; }
    inline float get_fov() const { return fovy_; }
    inline float get_aspect() const { return aspect_; }

    inline Mat4 get_view_matrix() const { return view_; }
    inline Mat4 get_projection_matrix() const { return proj_; }
    inline Mat4 get_prev_view_matrix() const { return prev_view_; }
    inline Mat4 get_prev_projection_matrix() const { return prev_proj_; }
    inline Mat4 get_inv_view_matrix() const { return view_.inverse(); }
    inline Mat4 get_inv_projection_matrix() const { return proj_.inverse(); }

    Frustum get_frustum() const { return frustum_; }

    void update_camera_info();

    static void register_class() {
        Registry::add<CameraComponent>("CameraComponent")
            .member("fovy", &CameraComponent::fovy_)
            .member("aspect", &CameraComponent::aspect_)
            .member("near", &CameraComponent::near_)
            .member("far", &CameraComponent::far_)
            ;
    }

private:
    float fovy_ = 90.0f;
    float aspect_ = 16.0f / 9.0f;
    float near_ = 0.1f;
    float far_ = 1000.0f;

    Vec3 position_ = Vec3::Zero();
    Vec3 front_ = Vec3::Zero();
    Vec3 up_ = Vec3::Zero();
    Vec3 right_ = Vec3::Zero();

    Mat4 view_;
    Mat4 proj_;
    Mat4 prev_view_;
    Mat4 prev_proj_;

    Frustum frustum_;
    CameraInfo camera_info_;
    bool move_ = false;

    void input_move(float delta_time);
    void update_matrix();
};

CEREAL_REGISTER_TYPE(CameraComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, CameraComponent);
