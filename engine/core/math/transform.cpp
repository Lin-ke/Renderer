#include "Transform.h"
#include <cmath>

Transform::Transform(const Mat4& matrix) {
    DirectX::XMVECTOR scale_vec;
    DirectX::XMVECTOR rotation_vec;
    DirectX::XMVECTOR translation_vec;

    if (DirectX::XMMatrixDecompose(&scale_vec, &rotation_vec, &translation_vec, matrix.load())) {
        scale_ = Vec3(scale_vec);
        rotation_ = Quaternion(rotation_vec).normalized();
        position_ = Vec3(translation_vec);
    } else {
        scale_ = Vec3::Ones();
        rotation_ = Quaternion::Identity();
        position_ = Vec3::Zero();
    }

    euler_angle_ = Math::to_euler_angle(rotation_);
    update_vector();
}

Mat4 Transform::get_matrix() const {
    DirectX::XMMATRIX scale = DirectX::XMMatrixScaling(scale_.x, scale_.y, scale_.z);
    DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationQuaternion(rotation_.load());
    DirectX::XMMATRIX translation = DirectX::XMMatrixTranslation(position_.x, position_.y, position_.z);
    return Mat4(scale * rotation * translation);
}

Mat4 Transform::get_inverse_matrix() const {
    return get_matrix().inverse();
}

void Transform::update_vector() {
    // Coordinate system: X = right, Y = up, Z = front
    Vec3 unitX = Vec3::UnitX();
    Vec3 unitY = Vec3::UnitY();
    Vec3 unitZ = Vec3::UnitZ();
    
    front_ = rotation_.rotate_vector(unitZ).normalized();
    up_ = rotation_.rotate_vector(unitY).normalized();
    right_ = rotation_.rotate_vector(unitX).normalized();
}

void Transform::set_rotation(const Quaternion& rotation) {
    rotation_ = rotation.normalized();
    euler_angle_ = Math::to_euler_angle(rotation_);
    update_vector();
}

void Transform::set_rotation(const Vec3& euler_angle) {
    euler_angle_ = euler_angle;
    rotation_ = Math::to_quaternion(euler_angle).normalized();
    update_vector();
}

Vec3 Transform::translate(const Vec3& translation) {
    position_ += translation;
    return position_;
}

Vec3 Transform::scale(const Vec3& scale_factor) {
    scale_.x *= scale_factor.x;
    scale_.y *= scale_factor.y;
    scale_.z *= scale_factor.z;
    return scale_;
}

Vec3 Transform::rotate(const Vec3& angle) {
    set_rotation(euler_angle_ + angle);
    return euler_angle_;
}
