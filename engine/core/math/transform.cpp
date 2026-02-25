#include "Transform.h"
#include <cmath>

Transform::Transform(const Mat4& matrix) {
    // Extract position from last column
    position_ = Vec3(matrix.m[0][3], matrix.m[1][3], matrix.m[2][3]);

    // Extract scale from column lengths (ignoring last row which is 0,0,0,1 for affine transforms)
    scale_.x = std::sqrt(matrix.m[0][0] * matrix.m[0][0] + matrix.m[1][0] * matrix.m[1][0] + matrix.m[2][0] * matrix.m[2][0]);
    scale_.y = std::sqrt(matrix.m[0][1] * matrix.m[0][1] + matrix.m[1][1] * matrix.m[1][1] + matrix.m[2][1] * matrix.m[2][1]);
    scale_.z = std::sqrt(matrix.m[0][2] * matrix.m[0][2] + matrix.m[1][2] * matrix.m[1][2] + matrix.m[2][2] * matrix.m[2][2]);

    // Extract rotation matrix (remove scale)
    if (scale_.x != 0.0f && scale_.y != 0.0f && scale_.z != 0.0f) {
        // Create rotation matrix by dividing by scale
        float r00 = matrix.m[0][0] / scale_.x;
        float r10 = matrix.m[1][0] / scale_.x;
        float r20 = matrix.m[2][0] / scale_.x;
        
        float r01 = matrix.m[0][1] / scale_.y;
        float r11 = matrix.m[1][1] / scale_.y;
        float r21 = matrix.m[2][1] / scale_.y;
        
        float r02 = matrix.m[0][2] / scale_.z;
        float r12 = matrix.m[1][2] / scale_.z;
        float r22 = matrix.m[2][2] / scale_.z;
        
        // Convert rotation matrix to quaternion
        // Using the trace method
        float trace = r00 + r11 + r22;
        float qw, qx, qy, qz;
        
        if (trace > 0.0f) {
            float s = 0.5f / std::sqrt(trace + 1.0f);
            qw = 0.25f / s;
            qx = (r21 - r12) * s;
            qy = (r02 - r20) * s;
            qz = (r10 - r01) * s;
        } else if (r00 > r11 && r00 > r22) {
            float s = 2.0f * std::sqrt(1.0f + r00 - r11 - r22);
            qw = (r21 - r12) / s;
            qx = 0.25f * s;
            qy = (r01 + r10) / s;
            qz = (r02 + r20) / s;
        } else if (r11 > r22) {
            float s = 2.0f * std::sqrt(1.0f + r11 - r00 - r22);
            qw = (r02 - r20) / s;
            qx = (r01 + r10) / s;
            qy = 0.25f * s;
            qz = (r12 + r21) / s;
        } else {
            float s = 2.0f * std::sqrt(1.0f + r22 - r00 - r11);
            qw = (r10 - r01) / s;
            qx = (r02 + r20) / s;
            qy = (r12 + r21) / s;
            qz = 0.25f * s;
        }
        
        rotation_ = Quaternion(qx, qy, qz, qw);
        rotation_.normalize();
    } else {
        rotation_ = Quaternion::Identity();
    }

    euler_angle_ = Math::to_euler_angle(rotation_);
    update_vector();
}

Mat4 Transform::get_matrix() const {
    // Build transformation matrix: T * R * S
    // First create rotation matrix from quaternion
    float x = rotation_.x;
    float y = rotation_.y;
    float z = rotation_.z;
    float w = rotation_.w;
    
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float wx = w * x, wy = w * y, wz = w * z;
    
    Mat4 transform;
    
    // First column (rotated X axis * scale)
    transform.m[0][0] = (1.0f - 2.0f * (yy + zz)) * scale_.x;
    transform.m[1][0] = (2.0f * (xy + wz)) * scale_.x;
    transform.m[2][0] = (2.0f * (xz - wy)) * scale_.x;
    transform.m[3][0] = 0.0f;
    
    // Second column (rotated Y axis * scale)
    transform.m[0][1] = (2.0f * (xy - wz)) * scale_.y;
    transform.m[1][1] = (1.0f - 2.0f * (xx + zz)) * scale_.y;
    transform.m[2][1] = (2.0f * (yz + wx)) * scale_.y;
    transform.m[3][1] = 0.0f;
    
    // Third column (rotated Z axis * scale)
    transform.m[0][2] = (2.0f * (xz + wy)) * scale_.z;
    transform.m[1][2] = (2.0f * (yz - wx)) * scale_.z;
    transform.m[2][2] = (1.0f - 2.0f * (xx + yy)) * scale_.z;
    transform.m[3][2] = 0.0f;
    
    // Fourth column (position)
    transform.m[0][3] = position_.x;
    transform.m[1][3] = position_.y;
    transform.m[2][3] = position_.z;
    transform.m[3][3] = 1.0f;
    
    return transform;
}

Mat4 Transform::get_inverse_matrix() const {
    return get_matrix().inverse();
}

void Transform::update_vector() {
    // Coordinate system: X = front, Y = up, Z = right
    // Rotation: pitch around Z (vertical), yaw around Y (horizontal), roll around X
    Vec3 unitX = Vec3::UnitX();
    Vec3 unitY = Vec3::UnitY();
    Vec3 unitZ = Vec3::UnitZ();
    
    front_ = rotation_.rotate_vector(unitX).normalized();
    up_ = rotation_.rotate_vector(unitY).normalized();
    right_ = rotation_.rotate_vector(unitZ).normalized();
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
