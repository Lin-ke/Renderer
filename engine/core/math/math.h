#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <intrin.h>
#include <cmath>
#include <algorithm>

#define PI 3.14159265358979323846f

// Forward declarations
struct Vec2;
struct Vec3;
struct Vec4;
struct Mat2;
struct Mat3;
struct Mat4;
struct Quaternion;

// Vector2
struct Vec2 {
    float x, y;
    
    Vec2() : x(0.0f), y(0.0f) {}
    Vec2(float vx, float vy) : x(vx), y(vy) {}
    explicit Vec2(float v) : x(v), y(v) {}
    Vec2(const DirectX::XMFLOAT2& v) : x(v.x), y(v.y) {}
    Vec2(const DirectX::XMVECTOR& v) { DirectX::XMStoreFloat2(reinterpret_cast<DirectX::XMFLOAT2*>(this), v); }
    
    static Vec2 Zero() { return Vec2(0.0f, 0.0f); }
    static Vec2 Ones() { return Vec2(1.0f, 1.0f); }
    static Vec2 UnitX() { return Vec2(1.0f, 0.0f); }
    static Vec2 UnitY() { return Vec2(0.0f, 1.0f); }
    
    // Eigen-compatible accessor methods (using operator() for component access)
    float operator()(int i) const { return (i == 0) ? x : y; }
    float& operator()(int i) { return (i == 0) ? x : y; }
    float operator[](int i) const { return (i == 0) ? x : y; }
    float& operator[](int i) { return (i == 0) ? x : y; }
    
    Vec2 operator+(const Vec2& other) const { return Vec2(x + other.x, y + other.y); }
    Vec2 operator-(const Vec2& other) const { return Vec2(x - other.x, y - other.y); }
    Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
    Vec2 operator*(const Vec2& other) const { return Vec2(x * other.x, y * other.y); }
    Vec2 operator/(float s) const { return Vec2(x / s, y / s); }
    Vec2 operator-() const { return Vec2(-x, -y); }
    
    Vec2& operator+=(const Vec2& other) { x += other.x; y += other.y; return *this; }
    Vec2& operator-=(const Vec2& other) { x -= other.x; y -= other.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    Vec2& operator/=(float s) { x /= s; y /= s; return *this; }
    
    float dot(const Vec2& other) const { return x * other.x + y * other.y; }
    float length() const { return std::sqrt(x * x + y * y); }
    float squared_length() const { return x * x + y * y; }
    Vec2 normalized() const { float len = length(); return (len > 0) ? Vec2(x / len, y / len) : Vec2::Zero(); }
    bool allFinite() const { return std::isfinite(x) && std::isfinite(y); }
    
    DirectX::XMVECTOR load() const { return DirectX::XMLoadFloat2(reinterpret_cast<const DirectX::XMFLOAT2*>(this)); }
    void store(DirectX::XMVECTOR v) { DirectX::XMStoreFloat2(reinterpret_cast<DirectX::XMFLOAT2*>(this), v); }
    
    operator DirectX::XMFLOAT2() const { return DirectX::XMFLOAT2(x, y); }
};

inline Vec2 operator*(float s, const Vec2& v) { return v * s; }

// Vector3
struct Vec3 {
    float x, y, z;
    
    Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vec3(float vx, float vy, float vz) : x(vx), y(vy), z(vz) {}
    explicit Vec3(float v) : x(v), y(v), z(v) {}
    Vec3(const DirectX::XMFLOAT3& v) : x(v.x), y(v.y), z(v.z) {}
    Vec3(const DirectX::XMVECTOR& v) { DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(this), v); }
    
    static Vec3 Zero() { return Vec3(0.0f, 0.0f, 0.0f); }
    static Vec3 Ones() { return Vec3(1.0f, 1.0f, 1.0f); }
    static Vec3 UnitX() { return Vec3(1.0f, 0.0f, 0.0f); }
    static Vec3 UnitY() { return Vec3(0.0f, 1.0f, 0.0f); }
    static Vec3 UnitZ() { return Vec3(0.0f, 0.0f, 1.0f); }
    
    // Eigen-compatible accessor methods
    float operator()(int i) const { return (i == 0) ? x : (i == 1) ? y : z; }
    float& operator()(int i) { return (i == 0) ? x : (i == 1) ? y : z; }
    float operator[](int i) const { return (i == 0) ? x : (i == 1) ? y : z; }
    float& operator[](int i) { return (i == 0) ? x : (i == 1) ? y : z; }
    
    Vec3 operator+(const Vec3& other) const { return Vec3(x + other.x, y + other.y, z + other.z); }
    Vec3 operator-(const Vec3& other) const { return Vec3(x - other.x, y - other.y, z - other.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator*(const Vec3& other) const { return Vec3(x * other.x, y * other.y, z * other.z); }
    Vec3 operator/(float s) const { return Vec3(x / s, y / s, z / s); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }
    
    Vec3& operator+=(const Vec3& other) { x += other.x; y += other.y; z += other.z; return *this; }
    Vec3& operator-=(const Vec3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
    
    float dot(const Vec3& other) const { return x * other.x + y * other.y + z * other.z; }
    Vec3 cross(const Vec3& other) const {
        return Vec3(
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        );
    }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float squared_length() const { return x * x + y * y + z * z; }
    float norm() const { return squared_length(); }
    Vec3 normalized() const { 
        float len = length(); 
        return (len > 0) ? Vec3(x / len, y / len, z / len) : Vec3::Zero(); 
    }
    bool allFinite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
    Vec3 cwiseMin(const Vec3& other) const { return Vec3((std::min)(x, other.x), (std::min)(y, other.y), (std::min)(z, other.z)); }
    Vec3 cwiseMax(const Vec3& other) const { return Vec3((std::max)(x, other.x), (std::max)(y, other.y), (std::max)(z, other.z)); }
    
    DirectX::XMVECTOR load() const { return DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(this)); }
    void store(DirectX::XMVECTOR v) { DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(this), v); }
    
    operator DirectX::XMFLOAT3() const { return DirectX::XMFLOAT3(x, y, z); }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

// Vector4
struct Vec4 {
    float x, y, z, w;
    
    Vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    Vec4(float vx, float vy, float vz, float vw) : x(vx), y(vy), z(vz), w(vw) {}
    Vec4(const Vec3& v, float vw) : x(v.x), y(v.y), z(v.z), w(vw) {}
    explicit Vec4(float v) : x(v), y(v), z(v), w(v) {}
    Vec4(const DirectX::XMFLOAT4& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}
    Vec4(const DirectX::XMVECTOR& v) { DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(this), v); }
    
    static Vec4 Zero() { return Vec4(0.0f, 0.0f, 0.0f, 0.0f); }
    static Vec4 Ones() { return Vec4(1.0f, 1.0f, 1.0f, 1.0f); }
    static Vec4 UnitX() { return Vec4(1.0f, 0.0f, 0.0f, 0.0f); }
    static Vec4 UnitY() { return Vec4(0.0f, 1.0f, 0.0f, 0.0f); }
    static Vec4 UnitZ() { return Vec4(0.0f, 0.0f, 1.0f, 0.0f); }
    static Vec4 UnitW() { return Vec4(0.0f, 0.0f, 0.0f, 1.0f); }
    
    // Eigen-compatible accessor methods
    float operator()(int i) const { return (i == 0) ? x : (i == 1) ? y : (i == 2) ? z : w; }
    float& operator()(int i) { return (i == 0) ? x : (i == 1) ? y : (i == 2) ? z : w; }
    float operator[](int i) const { return (i == 0) ? x : (i == 1) ? y : (i == 2) ? z : w; }
    float& operator[](int i) { return (i == 0) ? x : (i == 1) ? y : (i == 2) ? z : w; }
    
    Vec4 operator+(const Vec4& other) const { return Vec4(x + other.x, y + other.y, z + other.z, w + other.w); }
    Vec4 operator-(const Vec4& other) const { return Vec4(x - other.x, y - other.y, z - other.z, w - other.w); }
    Vec4 operator*(float s) const { return Vec4(x * s, y * s, z * s, w * s); }
    Vec4 operator*(const Vec4& other) const { return Vec4(x * other.x, y * other.y, z * other.z, w * other.w); }
    Vec4 operator/(float s) const { return Vec4(x / s, y / s, z / s, w / s); }
    Vec4 operator-() const { return Vec4(-x, -y, -z, -w); }
    
    Vec4& operator+=(const Vec4& other) { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
    Vec4& operator-=(const Vec4& other) { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
    Vec4& operator*=(float s) { x *= s; y *= s; z *= s; w *= s; return *this; }
    Vec4& operator/=(float s) { x /= s; y /= s; z /= s; w /= s; return *this; }
    
    float dot(const Vec4& other) const { return x * other.x + y * other.y + z * other.z + w * other.w; }
    float length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    float squared_length() const { return x * x + y * y + z * z + w * w; }
    Vec4 normalized() const { 
        float len = length(); 
        return (len > 0) ? Vec4(x / len, y / len, z / len, w / len) : Vec4::Zero(); 
    }
    bool allFinite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w); }
    
    Vec3 xyz() const { return Vec3(x, y, z); }
    
    DirectX::XMVECTOR load() const { return DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(this)); }
    void store(DirectX::XMVECTOR v) { DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(this), v); }
    
    operator DirectX::XMFLOAT4() const { return DirectX::XMFLOAT4(x, y, z, w); }
};

inline Vec4 operator*(float s, const Vec4& v) { return v * s; }

// Integer vectors
struct IVec2 {
    int x, y;
    IVec2() : x(0), y(0) {}
    IVec2(int vx, int vy) : x(vx), y(vy) {}
    explicit IVec2(int v) : x(v), y(v) {}
    static IVec2 Zero() { return IVec2(0, 0); }
    int operator[](int i) const { return (i == 0) ? x : y; }
    int& operator[](int i) { return (i == 0) ? x : y; }
};

struct IVec3 {
    int x, y, z;
    IVec3() : x(0), y(0), z(0) {}
    IVec3(int vx, int vy, int vz) : x(vx), y(vy), z(vz) {}
    explicit IVec3(int v) : x(v), y(v), z(v) {}
    static IVec3 Zero() { return IVec3(0, 0, 0); }
    int operator[](int i) const { return (i == 0) ? x : (i == 1) ? y : z; }
    int& operator[](int i) { return (i == 0) ? x : (i == 1) ? y : z; }
};

struct IVec4 {
    int x, y, z, w;
    IVec4() : x(0), y(0), z(0), w(0) {}
    IVec4(int vx, int vy, int vz, int vw) : x(vx), y(vy), z(vz), w(vw) {}
    explicit IVec4(int v) : x(v), y(v), z(v), w(v) {}
    static IVec4 Zero() { return IVec4(0, 0, 0, 0); }
    int operator[](int i) const { return (i == 0) ? x : (i == 1) ? y : (i == 2) ? z : w; }
    int& operator[](int i) { return (i == 0) ? x : (i == 1) ? y : (i == 2) ? z : w; }
};

// Unsigned integer vectors
struct UVec2 {
    uint32_t x, y;
    UVec2() : x(0), y(0) {}
    UVec2(uint32_t vx, uint32_t vy) : x(vx), y(vy) {}
    explicit UVec2(uint32_t v) : x(v), y(v) {}
    static UVec2 Zero() { return UVec2(0, 0); }
    uint32_t operator[](int i) const { return (i == 0) ? x : y; }
    uint32_t& operator[](int i) { return (i == 0) ? x : y; }
};

struct UVec3 {
    uint32_t x, y, z;
    UVec3() : x(0), y(0), z(0) {}
    UVec3(uint32_t vx, uint32_t vy, uint32_t vz) : x(vx), y(vy), z(vz) {}
    explicit UVec3(uint32_t v) : x(v), y(v), z(v) {}
    static UVec3 Zero() { return UVec3(0, 0, 0); }
    uint32_t operator[](int i) const { return (i == 0) ? x : (i == 1) ? y : z; }
    uint32_t& operator[](int i) { return (i == 0) ? x : (i == 1) ? y : z; }
};

struct UVec4 {
    uint32_t x, y, z, w;
    UVec4() : x(0), y(0), z(0), w(0) {}
    UVec4(uint32_t vx, uint32_t vy, uint32_t vz, uint32_t vw) : x(vx), y(vy), z(vz), w(vw) {}
    explicit UVec4(uint32_t v) : x(v), y(v), z(v), w(v) {}
    static UVec4 Zero() { return UVec4(0, 0, 0, 0); }
    uint32_t operator[](int i) const { return (i == 0) ? x : (i == 1) ? y : (i == 2) ? z : w; }
    uint32_t& operator[](int i) { return (i == 0) ? x : (i == 1) ? y : (i == 2) ? z : w; }
};

// Quaternion
struct Quaternion {
    float x, y, z, w;
    
    Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    Quaternion(float vx, float vy, float vz, float vw) : x(vx), y(vy), z(vz), w(vw) {}
    Quaternion(const DirectX::XMFLOAT4& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}
    Quaternion(const DirectX::XMVECTOR& v) { DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(this), v); }
    
    // Construct from axis and angle (angle in radians)
    Quaternion(const Vec3& axis, float angle) {
        DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(this), 
                               DirectX::XMQuaternionRotationAxis(axis.load(), angle));
    }
    
    static Quaternion Identity() { return Quaternion(0.0f, 0.0f, 0.0f, 1.0f); }
    
    Quaternion operator*(const Quaternion& other) const {
        DirectX::XMVECTOR q1 = load();
        DirectX::XMVECTOR q2 = other.load();
        return Quaternion(DirectX::XMQuaternionMultiply(q1, q2));
    }
    
    Quaternion& operator*=(const Quaternion& other) {
        *this = *this * other;
        return *this;
    }
    
    Quaternion conjugate() const {
        return Quaternion(-x, -y, -z, w);
    }
    
    Quaternion inverse() const {
        return Quaternion(DirectX::XMQuaternionInverse(load()));
    }
    
    float length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    float norm() const { return x * x + y * y + z * z + w * w; }
    
    Quaternion normalized() const {
        return Quaternion(DirectX::XMQuaternionNormalize(load()));
    }
    
    void normalize() {
        store(DirectX::XMQuaternionNormalize(load()));
    }
    
    // Rotate a vector by this quaternion
    Vec3 rotate_vector(const Vec3& v) const {
        DirectX::XMVECTOR q = load();
        DirectX::XMVECTOR vec = v.load();
        return Vec3(DirectX::XMVector3Rotate(vec, q));
    }
    
    DirectX::XMVECTOR load() const { return DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(this)); }
    void store(DirectX::XMVECTOR v) { DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(this), v); }
    
    operator DirectX::XMFLOAT4() const { return DirectX::XMFLOAT4(x, y, z, w); }
};

// 2x2 Matrix
struct Mat2 {
    float m[2][2];
    
    Mat2() = default;
    Mat2(float m00, float m01, float m10, float m11) {
        m[0][0] = m00; m[0][1] = m01;
        m[1][0] = m10; m[1][1] = m11;
    }
    
    static Mat2 Identity() {
        return Mat2(1.0f, 0.0f, 0.0f, 1.0f);
    }
    static Mat2 Zero() {
        return Mat2(0.0f, 0.0f, 0.0f, 0.0f);
    }
    
    float operator()(int row, int col) const { return m[row][col]; }
    float& operator()(int row, int col) { return m[row][col]; }
    
    Vec2 col(int i) const { return Vec2(m[0][i], m[1][i]); }
    Vec2 row(int i) const { return Vec2(m[i][0], m[i][1]); }
    
    void set_col(int i, const Vec2& v) { m[0][i] = v.x; m[1][i] = v.y; }
    void set_row(int i, const Vec2& v) { m[i][0] = v.x; m[i][1] = v.y; }
};

// 3x3 Matrix
struct Mat3 {
    float m[3][3];
    
    Mat3() = default;
    
    static Mat3 Identity() {
        Mat3 mat;
        // Manual initialization to avoid recursion
        mat.m[0][0] = 1.0f; mat.m[0][1] = 0.0f; mat.m[0][2] = 0.0f;
        mat.m[1][0] = 0.0f; mat.m[1][1] = 1.0f; mat.m[1][2] = 0.0f;
        mat.m[2][0] = 0.0f; mat.m[2][1] = 0.0f; mat.m[2][2] = 1.0f;
        return mat;
    }
    static Mat3 Zero() {
        Mat3 mat;
        // Manual initialization to avoid recursion
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                mat.m[i][j] = 0.0f;
        return mat;
    }
    
    float operator()(int row, int col) const { return m[row][col]; }
    float& operator()(int row, int col) { return m[row][col]; }
    
    Vec3 col(int i) const { return Vec3(m[0][i], m[1][i], m[2][i]); }
    Vec3 row(int i) const { return Vec3(m[i][0], m[i][1], m[i][2]); }
    
    void set_col(int i, const Vec3& v) { m[0][i] = v.x; m[1][i] = v.y; m[2][i] = v.z; }
    void set_row(int i, const Vec3& v) { m[i][0] = v.x; m[i][1] = v.y; m[i][2] = v.z; }
};

// 4x4 Matrix
struct Mat4 {
    float m[4][4];
    
    Mat4() = default;
    Mat4(const DirectX::XMFLOAT4X4& mat) {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                m[i][j] = mat.m[i][j];
    }
    Mat4(const DirectX::XMMATRIX& mat) { DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(this), mat); }
    
    static Mat4 Identity() {
        Mat4 mat;
        // Manual initialization to avoid recursion through constructor
        mat.m[0][0] = 1.0f; mat.m[0][1] = 0.0f; mat.m[0][2] = 0.0f; mat.m[0][3] = 0.0f;
        mat.m[1][0] = 0.0f; mat.m[1][1] = 1.0f; mat.m[1][2] = 0.0f; mat.m[1][3] = 0.0f;
        mat.m[2][0] = 0.0f; mat.m[2][1] = 0.0f; mat.m[2][2] = 1.0f; mat.m[2][3] = 0.0f;
        mat.m[3][0] = 0.0f; mat.m[3][1] = 0.0f; mat.m[3][2] = 0.0f; mat.m[3][3] = 1.0f;
        return mat;
    }
    static Mat4 Zero() {
        Mat4 mat;
        // Manual initialization to avoid recursion
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                mat.m[i][j] = 0.0f;
        return mat;
    }
    
    float operator()(int row, int col) const { return m[row][col]; }
    float& operator()(int row, int col) { return m[row][col]; }
    
    Vec4 col(int i) const { return Vec4(m[0][i], m[1][i], m[2][i], m[3][i]); }
    Vec4 row(int i) const { return Vec4(m[i][0], m[i][1], m[i][2], m[i][3]); }
    
    void set_col(int i, const Vec4& v) { m[0][i] = v.x; m[1][i] = v.y; m[2][i] = v.z; m[3][i] = v.w; }
    void set_row(int i, const Vec4& v) { m[i][0] = v.x; m[i][1] = v.y; m[i][2] = v.z; m[i][3] = v.w; }
    
    Mat4 operator*(const Mat4& other) const {
        DirectX::XMMATRIX m1 = load();
        DirectX::XMMATRIX m2 = other.load();
        return Mat4(m1 * m2);
    }
    
    Vec4 operator*(const Vec4& v) const {
        DirectX::XMMATRIX mat = load();
        DirectX::XMVECTOR vec = v.load();
        return Vec4(DirectX::XMVector4Transform(vec, mat));
    }
    
    Mat4 inverse() const {
        DirectX::XMMATRIX mat = load();
        return Mat4(DirectX::XMMatrixInverse(nullptr, mat));
    }
    
    Mat4 transpose() const {
        DirectX::XMMATRIX mat = load();
        return Mat4(DirectX::XMMatrixTranspose(mat));
    }
    
    bool operator==(const Mat4& other) const {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (m[i][j] != other.m[i][j])
                    return false;
        return true;
    }
    
    bool operator!=(const Mat4& other) const {
        return !(*this == other);
    }
    
    DirectX::XMMATRIX load() const { return DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(this)); }
    void store(DirectX::XMMATRIX mat) { DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(this), mat); }
    
    operator DirectX::XMFLOAT4X4() const {
        DirectX::XMFLOAT4X4 result;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                result.m[i][j] = m[i][j];
        return result;
    }
};

inline Vec4 operator*(const Vec4& v, const Mat4& m) {
    DirectX::XMVECTOR vec = v.load();
    DirectX::XMMATRIX mat = m.load();
    return Vec4(DirectX::XMVector4Transform(vec, mat));
}

namespace Math 
{
    inline float to_radians(float angle) { return angle * PI / 180.0f; }
    inline Vec2 to_radians(Vec2 angle) { return Vec2(to_radians(angle.x), to_radians(angle.y)); }
    inline Vec3 to_radians(Vec3 angle) { return Vec3(to_radians(angle.x), to_radians(angle.y), to_radians(angle.z)); }
    inline Vec4 to_radians(Vec4 angle) { return Vec4(to_radians(angle.x), to_radians(angle.y), to_radians(angle.z), to_radians(angle.w)); }

    inline float to_angle(float radians) { return radians * 180.0f / PI; }
    inline Vec2 to_angle(Vec2 radians) { return Vec2(to_angle(radians.x), to_angle(radians.y)); }
    inline Vec3 to_angle(Vec3 radians) { return Vec3(to_angle(radians.x), to_angle(radians.y), to_angle(radians.z)); }
    inline Vec4 to_angle(Vec4 radians) { return Vec4(to_angle(radians.x), to_angle(radians.y), to_angle(radians.z), to_angle(radians.w)); }

    inline uint32_t align(uint32_t value, uint32_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    inline bool is_nan(float a) { return _isnan(a) != 0; }
    inline bool is_nan(double a) { return _isnan(a) != 0; }
    inline bool is_finite(float a) { return _finite(a) != 0; }
    inline bool is_finite(double a) { return _finite(a) != 0; }

    inline uint64_t count_leading_zeros_64(uint64_t value)
    {
        unsigned long bit_index;
        if (!_BitScanReverse64(&bit_index, value)) bit_index = -1;
        return 63 - bit_index;
    }

    inline uint64_t count_trailing_zeros_64(uint64_t value)
    {
        unsigned long bit_index;
        return _BitScanForward64(&bit_index, value) ? bit_index : 64;
    }

    inline uint32_t count_leading_zeros(uint32_t value)
    {
        unsigned long bit_index;
        _BitScanReverse64(&bit_index, uint64_t(value) * 2 + 1);
        return 32 - bit_index;
    }

    inline uint64_t ceil_log_two_64(uint64_t arg)
    {
        arg = arg ? arg : 1;
        return 64 - count_leading_zeros_64(arg - 1);
    }

    inline uint32_t floor_log2(uint32_t value)
    {
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
        unsigned long bit_index;
        return _BitScanForward(&bit_index, value) ? bit_index : 32;
    }

    inline uint32_t ceil_log_two(uint32_t arg)
    {
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

    Vec3 extract_euler_angles(const Mat3& m);

    Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up);

    Mat4 perspective(float fovy, float aspect, float near_plane, float far_plane);

    Mat4 ortho(float left, float right, float bottom, float top, float near_plane, float far_plane);

    void mat3x4(Mat4 mat, float* new_mat);
}
