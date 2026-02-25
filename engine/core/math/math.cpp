#include "Math.h"
#include <cmath>

namespace Math 
{
    Vec3 clamp_euler_angle(Vec3 angle)
    {
        // angle is (pitch, yaw, roll)
        // pitch = rotation around Z axis (vertical look)
        // yaw = rotation around Y axis (horizontal look)
        // roll = rotation around X axis (tilt)
        Vec3 ret = angle;
        
        // Clamp pitch (x component) to prevent gimbal lock at +/- 90 degrees
        // This limits how far up/down you can look
        if(ret.x > 89.9f) ret.x = 89.9f;
        if(ret.x < -89.9f) ret.x = -89.9f;

        // Normalize yaw (y) to [-180, 180]
        ret.y = fmod(ret.y, 360.0f);
        if(ret.y > 180.0f) ret.y -= 360.0f;
        if(ret.y < -180.0f) ret.y += 360.0f;
        
        // Normalize roll (z) to [-180, 180]
        ret.z = fmod(ret.z, 360.0f);
        if(ret.z > 180.0f) ret.z -= 360.0f;
        if(ret.z < -180.0f) ret.z += 360.0f;

        return ret;
    }

    Vec3 to_euler_angle(const Quaternion& q)
    {
        // Returns (pitch, yaw, roll) in degrees
        // Mapping to axes:
        // - pitch = rotation around Z axis (vertical look, because X is front)
        // - yaw = rotation around Y axis (horizontal look)
        // - roll = rotation around X axis (tilt)
        
        // For Z * Y * X rotation order (pitch around Z, yaw around Y, roll around X):
        // The rotation matrix elements are:
        // [ cy*cr+sy*sp*sr,  sr*cy-sp*sy*cr,  sp*cy ]
        // [ -sr*cy+sp*sy*cr, cy*cr-sy*sp*sr,  cp*sy ]
        // [ -cp*sr,          -cp*cr,          cp*cy ]
        
        // Roll (rotation around X - last axis in Z*Y*X order)
        float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
        float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
        float roll = std::atan2(sinr_cosp, cosr_cosp);

        // Pitch (rotation around Z - first axis in Z*Y*X order)  
        float sinp = 2.0f * (q.w * q.z - q.x * q.y);
        float pitch;
        if (std::abs(sinp) >= 1.0f)
            pitch = std::copysign(PI / 2.0f, sinp);
        else
            pitch = std::asin(sinp);

        // Yaw (rotation around Y - middle axis in Z*Y*X order)
        float siny_cosp = 2.0f * (q.w * q.y + q.z * q.x);
        float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
        float yaw = std::atan2(siny_cosp, cosy_cosp);

        // Return (pitch, yaw, roll) - matching to_quaternion's parameter order
        return Vec3(pitch * 180.0f / PI, yaw * 180.0f / PI, roll * 180.0f / PI);
    }

    Quaternion to_quaternion(Vec3 euler_angle)
    {
        // euler_angle is (pitch, yaw, roll) in degrees
        // BUT: Our coordinate system has X as front, so:
        // - yaw (horizontal look) = rotation around Y axis (up)
        // - pitch (vertical look) = rotation around Z axis (right) 
        //   (NOT X! Because rotating around X/front doesn't change front direction)
        // - roll (tilt head) = rotation around X axis (front)
        
        Vec3 radians = to_radians(euler_angle);
        
        // Convert to quaternion using Z * Y * X order (pitch around Z, yaw around Y, roll around X)
        // DXMath: XMQuaternionRotationRollPitchYaw(pitch, yaw, roll) rotates around X, then Y, then Z
        // We want: pitch around Z, yaw around Y, roll around X
        // So we use: XMQuaternionRotationRollPitchYaw(roll, yaw, pitch) 
        // which = rotate X(roll), then Y(yaw), then Z(pitch)
        return Quaternion(DirectX::XMQuaternionRotationRollPitchYaw(radians.z, radians.y, radians.x));
    }

    Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up)
    {
        // Manual implementation to match Eigen's column-major format
        Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);

        Mat4 mat;
        // Column-major layout (matching Eigen)
        mat.m[0][0] = s.x;  mat.m[1][0] = s.y;  mat.m[2][0] = s.z;  mat.m[3][0] = -s.dot(eye);
        mat.m[0][1] = u.x;  mat.m[1][1] = u.y;  mat.m[2][1] = u.z;  mat.m[3][1] = -u.dot(eye);
        mat.m[0][2] = f.x;  mat.m[1][2] = f.y;  mat.m[2][2] = f.z;  mat.m[3][2] = -f.dot(eye);
        mat.m[0][3] = 0.0f; mat.m[1][3] = 0.0f; mat.m[2][3] = 0.0f; mat.m[3][3] = 1.0f;
        return mat;
    }

    Mat4 perspective(float fovy, float aspect, float near_plane, float far_plane)
    {
        // Manual implementation to match Eigen's column-major format
        float tan_half_fovy = std::tan(fovy / 2.0f);
        
        Mat4 mat;
        // Zero out all elements first
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                mat.m[i][j] = 0.0f;
        
        // Column-major layout (matching Eigen)
        mat.m[0][0] = 1.0f / (aspect * tan_half_fovy);
        mat.m[1][1] = 1.0f / tan_half_fovy;
        mat.m[2][2] = far_plane / (far_plane - near_plane);
        mat.m[3][2] = -(far_plane * near_plane) / (far_plane - near_plane);
        mat.m[2][3] = 1.0f;
        
        return mat;
    }

    Mat4 ortho(float left, float right, float bottom, float top, float near_plane, float far_plane)
    {
        // Manual implementation to match Eigen's column-major format
        Mat4 mat;
        
        // Column-major layout (matching Eigen)
        mat.m[0][0] = 2.0f / (right - left);
        mat.m[1][0] = 0.0f;
        mat.m[2][0] = 0.0f;
        mat.m[3][0] = -(right + left) / (right - left);
        
        mat.m[0][1] = 0.0f;
        mat.m[1][1] = 2.0f / (top - bottom);
        mat.m[2][1] = 0.0f;
        mat.m[3][1] = -(top + bottom) / (top - bottom);
        
        mat.m[0][2] = 0.0f;
        mat.m[1][2] = 0.0f;
        mat.m[2][2] = -1.0f / (far_plane - near_plane);
        mat.m[3][2] = -near_plane / (far_plane - near_plane);
        
        mat.m[0][3] = 0.0f;
        mat.m[1][3] = 0.0f;
        mat.m[2][3] = 0.0f;
        mat.m[3][3] = 1.0f;
        
        return mat;
    }

    void mat3x4(Mat4 mat, float* new_mat)
    {
        // Extract 3x4 matrix (3 rows, 4 columns) from 4x4
        // Store in row-major order for each row
        new_mat[0]  = mat.m[0][0];
        new_mat[1]  = mat.m[0][1];
        new_mat[2]  = mat.m[0][2];
        new_mat[3]  = mat.m[0][3];
        new_mat[4]  = mat.m[1][0];
        new_mat[5]  = mat.m[1][1];
        new_mat[6]  = mat.m[1][2];
        new_mat[7]  = mat.m[1][3];
        new_mat[8]  = mat.m[2][0];
        new_mat[9]  = mat.m[2][1];
        new_mat[10] = mat.m[2][2];
        new_mat[11] = mat.m[2][3];
    }

    Vec3 extract_euler_angles(const Mat3& m)
    {
        Vec3 angles;
        
        // XYZ order: pitch = x, yaw = y, roll = z
        angles.x = std::atan2(m.m[2][1], m.m[2][2]) * 180.0f / PI;
        
        float c2 = std::sqrt(m.m[0][0] * m.m[0][0] + m.m[1][0] * m.m[1][0]);
        angles.y = std::atan2(-m.m[2][0], c2) * 180.0f / PI;
        
        angles.z = std::atan2(m.m[1][0], m.m[0][0]) * 180.0f / PI;
        
        return angles;
    }
}
