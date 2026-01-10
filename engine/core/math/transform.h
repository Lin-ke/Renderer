#ifndef Transform_H
#define Transform_H
#include "engine/core/math/Math.h"

// 坐标系定义
// World Space:      +X Forward, +Y Up, +Z Right
// View Space:       +X Right,   +Y Up, -Z Forward
// Clip Space:       +X Right,   -Y Up, +Z Forward
// NDC Space:        Top-Left [-1, -1], Bottom-Right [1, 1]
// Screen Space:     Top-Left [0, 0],   Bottom-Right [1, 1]

class Transform {
 public:
  Transform() = default;

  // 从矩阵构造 (显式构造，防止隐式转换)
  explicit Transform(const Mat4& matrix);

  // 构造：位置，缩放，四元数
  Transform(const Vec3& position, const Vec3& scale,
            const Quaternion& rotation = Quaternion::Identity())
      : position_(position),
        scale_(scale),
        rotation_(rotation),
        euler_angle_(Math::to_euler_angle(rotation)) {
    update_vector();
  }

  // 构造：位置，缩放，欧拉角
  Transform(const Vec3& position, const Vec3& scale,
            const Vec3& euler_angle = Vec3::Zero())
      : position_(position),
        scale_(scale),
        euler_angle_(euler_angle),
        rotation_(Math::to_quaternion(euler_angle)) {
    update_vector();
  }

  // Getters
  inline Vec3 get_position() const { return position_; }
  inline Vec3 get_scale() const { return scale_; }
  inline Quaternion get_rotation() const { return rotation_; }
  inline Vec3 get_euler_angle() const { return euler_angle_; }

  // Direction Vectors
  inline Vec3 front() const { return front_; }
  inline Vec3 up() const { return up_; }
  inline Vec3 right() const { return right_; }

  // Setters
  void set_position(const Vec3& position) { position_ = position; }
  void set_scale(const Vec3& scale) { scale_ = scale; }
  void set_rotation(const Quaternion& rotation);
  void set_rotation(const Vec3& euler_angle);

  // Operations
  Vec3 translate(const Vec3& translation);
  Vec3 scale(const Vec3& scale_factor);
  Vec3 rotate(const Vec3& angle);

  // Matrix
  Mat4 get_matrix() const;
  Mat4 get_inverse_matrix() const;

  // Helpers
  Quaternion inverse_rotation() const { return rotation_.conjugate(); }
  
  Vec3 inverse_scale() const {
    // 防止除零，通常建议确保 scale 不为 0
    return Vec3(1.0f / scale_.x(), 1.0f / scale_.y(), 1.0f / scale_.z());
  }
  
  Vec3 inverse_position() const { return -position_; }
  
  Transform inverse() const {
    return Transform(inverse_position(), inverse_scale(), inverse_rotation());
  }

  Transform operator*(const Transform& other) const {
    return Transform(get_matrix() * other.get_matrix());
  }

 private:
  void update_vector();

 private:
  Vec3 position_ = Vec3::Zero();
  Vec3 scale_ = Vec3::Ones();
  Quaternion rotation_ = Quaternion::Identity();
  
  // ZYX顺序的欧拉角，角度制 (Yaw, Pitch, Roll)
  Vec3 euler_angle_ = Vec3::Zero(); 

  // 缓存的方向向量
  Vec3 front_ = Vec3::UnitX();
  Vec3 up_ = Vec3::UnitY();
  Vec3 right_ = Vec3::UnitZ();
};

#endif