#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <cstdint>
#include <intrin.h> // _BitScanReverse 等函数通常需要这个

#define PI 3.14159265358979323846

// 注意:    Eigen的矩阵是行优先 row
//          glm的矩阵是列优先 column

typedef Eigen::Matrix2f Mat2;
typedef Eigen::Matrix3f Mat3;
typedef Eigen::Matrix4f Mat4;

typedef Eigen::Vector2f Vec2;
typedef Eigen::Vector3f Vec3;
typedef Eigen::Vector4f Vec4;

typedef Eigen::Vector2i IVec2;
typedef Eigen::Vector3i IVec3;
typedef Eigen::Vector4i IVec4;

typedef Eigen::Vector2<uint32_t> UVec2;
typedef Eigen::Vector3<uint32_t> UVec3;
typedef Eigen::Vector4<uint32_t> UVec4;

typedef Eigen::Quaternion<float> Quaternion;

namespace Math 
{
    inline float to_radians(float angle) { return angle * PI / 180.0f; }
    inline Vec2 to_radians(Vec2 angle) { return angle * PI / 180.0f; }
    inline Vec3 to_radians(Vec3 angle) { return angle * PI / 180.0f; }
    inline Vec4 to_radians(Vec4 angle) { return angle * PI / 180.0f; }

    inline float to_angle(float radians) { return radians * 180.0f / PI; }
    inline Vec2 to_angle(Vec2 radians) { return radians * 180.0f / PI; }
    inline Vec3 to_angle(Vec3 radians) { return radians * 180.0f / PI; }
    inline Vec4 to_angle(Vec4 radians) { return radians * 180.0f / PI; }

    inline uint32_t align(uint32_t value, uint32_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    //UE5 MicrosoftPlatformMath.h

    inline bool is_nan(float a) { return _isnan(a) != 0; }
    inline bool is_nan(double a) { return _isnan(a) != 0; }
    inline bool is_finite(float a) { return _finite(a) != 0; }
    inline bool is_finite(double a) { return _finite(a) != 0; }

    inline uint64_t count_leading_zeros_64(uint64_t value)
    {
        //https://godbolt.org/z/Ejh5G4vPK   
        // return 64 if value if was 0
        unsigned long bit_index;
        if (!_BitScanReverse64(&bit_index, value)) bit_index = -1;
        return 63 - bit_index;
    }

    inline uint64_t count_trailing_zeros_64(uint64_t value)
    {
        // return 64 if Value is 0
        unsigned long bit_index; // 0-based, where the LSB is 0 and MSB is 63
        return _BitScanForward64(&bit_index, value) ? bit_index : 64;
    }

    inline uint32_t count_leading_zeros(uint32_t value)
    {
        // return 32 if value is zero
        unsigned long bit_index;
        _BitScanReverse64(&bit_index, uint64_t(value) * 2 + 1);
        return 32 - bit_index;
    }

    inline uint64_t ceil_log_two_64(uint64_t arg)
    {
        // if Arg is 0, change it to 1 so that we return 0
        arg = arg ? arg : 1;
        return 64 - count_leading_zeros_64(arg - 1);
    }

    inline uint32_t floor_log2(uint32_t value)
    {
        // Use BSR to return the log2 of the integer
        // return 0 if value is 0
        unsigned long bit_index;
        return _BitScanReverse(&bit_index, value) ? bit_index : 0;
    }

    inline uint8_t count_leading_zeros_8(uint8_t value)
    {
        unsigned long bit_index;
        _BitScanReverse(&bit_index, uint32_t(value) * 2 + 1);
        return uint8_t(8 - bit_index);
    }

    inline uint32_t count_trailing_zeros(uint32_t value)
    {
        // return 32 if value was 0
        unsigned long bit_index; // 0-based, where the LSB is 0 and MSB is 31
        return _BitScanForward(&bit_index, value) ? bit_index : 32;
    }

    inline uint32_t ceil_log_two(uint32_t arg)
    {
        // if Arg is 0, change it to 1 so that we return 0
        arg = arg ? arg : 1;
        return 32 - count_leading_zeros(arg - 1);
    }

    inline uint32_t round_up_to_power_of_two(uint32_t arg)
    {
        return 1 << ceil_log_two(arg);
    }

    inline uint64_t round_up_to_power_of_two_64(uint64_t arg)
    {
        return uint64_t(1) << ceil_log_two_64(arg);
    }

    inline uint64_t floor_log2_64(uint64_t value)
    {
        unsigned long bit_index;
        return _BitScanReverse64(&bit_index, value) ? bit_index : 0;
    }

    Vec3 clamp_euler_angle(Vec3 angle);

    Vec3 to_euler_angle(const Quaternion& q);  

    Quaternion to_quaternion(Vec3 euler_angle);

    /**
     * @brief Extract Euler angles from rotation matrix
     * @param m 3x3 rotation matrix
     * @return Euler angles in degrees (XYZ order)
     */
    Vec3 extract_euler_angles(const Mat3& m);

    Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up);

    Mat4 perspective(float fovy, float aspect, float near_plane, float far_plane);

    Mat4 ortho(float left, float right, float bottom, float top, float near_plane, float far_plane);

    void mat3x4(Mat4 mat, float* new_mat);
}
