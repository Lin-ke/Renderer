#include "Transform.h"

Transform::Transform(const Mat4& matrix) {
  
  position_ = matrix.block<3, 1>(0, 3);

  
  scale_(0) = std::sqrt(std::pow(matrix(0, 0), 2) + std::pow(matrix(1, 0), 2) + std::pow(matrix(2, 0), 2));
  scale_(1) = std::sqrt(std::pow(matrix(0, 1), 2) + std::pow(matrix(1, 1), 2) + std::pow(matrix(2, 1), 2));
  scale_(2) = std::sqrt(std::pow(matrix(0, 2), 2) + std::pow(matrix(1, 2), 2) + std::pow(matrix(2, 2), 2));

  
  if (scale_.x() != 0 && scale_.y() != 0 && scale_.z() != 0) {
      rotation_ = Quaternion(matrix.block<3, 3>(0, 0) * scale_.asDiagonal().inverse());
      rotation_.normalize();
  } else {
      rotation_ = Quaternion::Identity();
  }

  euler_angle_ = Math::to_euler_angle(rotation_);
  update_vector();
}

Mat4 Transform::get_matrix() const {
  Mat4 transform = Mat4::Identity();
  
  transform.block<3, 3>(0, 0) = rotation_.toRotationMatrix() * scale_.asDiagonal();
  transform.block<3, 1>(0, 3) = position_;
  return transform;
}

Mat4 Transform::get_inverse_matrix() const {
  // 直接求逆
  return get_matrix().inverse();
}

void Transform::update_vector() {
  
  front_ = (rotation_.toRotationMatrix() * Vec3::UnitX()).normalized();
  up_    = (rotation_.toRotationMatrix() * Vec3::UnitY()).normalized();
  right_ = (rotation_.toRotationMatrix() * Vec3::UnitZ()).normalized();
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
  
  scale_ = scale_.array() * scale_factor.array();
  return scale_;
}

Vec3 Transform::rotate(const Vec3& angle) {
  
  set_rotation(euler_angle_ + angle);
  return euler_angle_;
}