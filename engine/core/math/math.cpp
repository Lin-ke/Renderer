#include "Math.h"
#include <cmath>

namespace Math 
{
    Vec3 clamp_euler_angle(Vec3 angle)
    {
        // angle is (pitch, yaw, roll)
        // pitch = rotation around X axis (vertical look)
        // yaw = rotation around Y axis (horizontal look)
        // roll = rotation around Z axis (tilt)
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
        // - pitch = rotation around X axis
        // - yaw = rotation around Y axis
        // - roll = rotation around Z axis
        float sin_pitch = 2.0f * (q.w * q.x + q.y * q.z);
        float cos_pitch = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
        float pitch = std::atan2(sin_pitch, cos_pitch);

        float sin_yaw = 2.0f * (q.w * q.y - q.z * q.x);
        float yaw;
        if (std::abs(sin_yaw) >= 1.0f)
            yaw = std::copysign(PI / 2.0f, sin_yaw);
        else
            yaw = std::asin(sin_yaw);

        float sin_roll = 2.0f * (q.w * q.z + q.x * q.y);
        float cos_roll = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
        float roll = std::atan2(sin_roll, cos_roll);

        return Vec3(pitch * 180.0f / PI, yaw * 180.0f / PI, roll * 180.0f / PI);
    }

    Quaternion to_quaternion(Vec3 euler_angle)
    {
        // euler_angle is (pitch, yaw, roll) in degrees
        // pitch around X, yaw around Y, roll around Z
        Vec3 radians = to_radians(euler_angle);
        return Quaternion(DirectX::XMQuaternionRotationRollPitchYaw(radians.x, radians.y, radians.z));
    }

    Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up)
    {
        return Mat4(DirectX::XMMatrixLookAtLH(eye.load(), center.load(), up.load()));
    }

    Mat4 perspective(float fovy, float aspect, float near_plane, float far_plane)
    {
        return Mat4(DirectX::XMMatrixPerspectiveFovLH(fovy, aspect, near_plane, far_plane));
    }

    Mat4 ortho(float left, float right, float bottom, float top, float near_plane, float far_plane)
    {
        return Mat4(DirectX::XMMatrixOrthographicOffCenterLH(left, right, bottom, top, near_plane, far_plane));
    }

    void mat3x4(Mat4 mat, float* new_mat)
    {
        // Extract 3x4 matrix (3 rows, 4 columns) from 4x4
        // In Row-Major (v * M), the 4th row (m[3]) is translation.
        // However, DXR/Shader 3x4 structures usually expect [Basis | Translation].
        // To be compatible with RHIAccelerationStructureInstanceInfo which uses float[3][4],
        // we extract the 3 basis vectors and the translation.
        
        // Row 0: X-basis (m00, m01, m02) and Translation.x (m30) ? 
        // No, typically float[3][4] in DXR is the top 3x4 of a COLUMN-MAJOR matrix.
        // For a ROW-MAJOR mat (v*M), the top-left 3x3 is rotation/scale, and m[3][0,1,2] is translation.
        // The DXR instance transform is actually the transpose of a row-major world matrix.
        
        // Correct mapping for row-major M to DXR 3x4 (which is essentially M transposed, then take top 3 rows):
        new_mat[0]  = mat.m[0][0]; new_mat[1]  = mat.m[1][0]; new_mat[2]  = mat.m[2][0]; new_mat[3]  = mat.m[3][0];
        new_mat[4]  = mat.m[0][1]; new_mat[5]  = mat.m[1][1]; new_mat[6]  = mat.m[2][1]; new_mat[7]  = mat.m[3][1];
        new_mat[8]  = mat.m[0][2]; new_mat[9]  = mat.m[1][2]; new_mat[10] = mat.m[2][2]; new_mat[11] = mat.m[3][2];
    }

    Vec3 extract_euler_angles(const Mat3& m)
    {
        Mat4 full = Mat4::Identity();
        full.m[0][0] = m.m[0][0]; full.m[0][1] = m.m[0][1]; full.m[0][2] = m.m[0][2];
        full.m[1][0] = m.m[1][0]; full.m[1][1] = m.m[1][1]; full.m[1][2] = m.m[1][2];
        full.m[2][0] = m.m[2][0]; full.m[2][1] = m.m[2][1]; full.m[2][2] = m.m[2][2];
        Quaternion q(DirectX::XMQuaternionRotationMatrix(full.load()));
        return to_euler_angle(q.normalized());
    }
}
