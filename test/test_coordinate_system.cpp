#include <catch2/catch_test_macros.hpp>
#include "engine/core/math/math.h"
#include <cmath>

// 验证坐标系的正确性
// 世界空间: X=前, Y=上, Z=右 (右手系)
TEST_CASE("View Matrix Coordinate System", "[math]") {
    // 相机在原点，朝向 +X（世界前向）
    Vec3 eye(0, 0, 0);
    Vec3 center(1, 0, 0);  // 看向 +X
    Vec3 up(0, 1, 0);      // Y 是上
    
    Mat4 view = Math::look_at(eye, center, up);
    
    // 验证：世界空间的基向量经过 View 变换后应该映射到观察空间的正确轴向
    
    // 世界 +X(前) 应该映射到观察 +Z(前，相机前方)
    Vec4 world_front = view * Vec4(1, 0, 0, 1);
    REQUIRE(std::abs(world_front.x) < 0.001f);  // X 分量应为 0
    REQUIRE(std::abs(world_front.y) < 0.001f);  // Y 分量应为 0
    REQUIRE(world_front.z > 0.9f);              // Z 分量应为正（相机前方）
    
    // 世界 +Y(上) 应该映射到观察 +Y(上)
    Vec4 world_up = view * Vec4(0, 1, 0, 1);
    REQUIRE(std::abs(world_up.x) < 0.001f);
    REQUIRE(world_up.y > 0.9f);                 // Y 分量应为正
    REQUIRE(std::abs(world_up.z) < 0.001f);
    
    // 世界 +Z(右) 应该映射到观察 +X(右)
    Vec4 world_right = view * Vec4(0, 0, 1, 1);
    REQUIRE(world_right.x > 0.9f);              // X 分量应为正
    REQUIRE(std::abs(world_right.y) < 0.001f);
    REQUIRE(std::abs(world_right.z) < 0.001f);
}

// 验证 View 矩阵是右手系还是左手系
TEST_CASE("View Matrix Handedness", "[math]") {
    Vec3 eye(0, 0, 0);
    Vec3 center(1, 0, 0);
    Vec3 up(0, 1, 0);
    
    Mat4 view = Math::look_at(eye, center, up);
    
    // 提取 View 矩阵的基向量（列向量）
    Vec3 vx(view.m[0][0], view.m[0][1], view.m[0][2]);  // 观察空间 X 轴在世界空间的表示
    Vec3 vy(view.m[1][0], view.m[1][1], view.m[1][2]);  // 观察空间 Y 轴在世界空间的表示
    Vec3 vz(view.m[2][0], view.m[2][1], view.m[2][2]);  // 观察空间 Z 轴在世界空间的表示
    
    // 检查右手系: X × Y = Z
    Vec3 cross_xy = vx.cross(vy);
    float dot_z = cross_xy.dot(vz);
    
    // 如果 dot_z > 0，观察空间是右手系；如果 < 0，是左手系
    INFO("Cross(X,Y) dot Z = " << dot_z);
    INFO("View X axis in world: (" << vx.x << ", " << vx.y << ", " << vx.z << ")");
    INFO("View Y axis in world: (" << vy.x << ", " << vy.y << ", " << vy.z << ")");
    INFO("View Z axis in world: (" << vz.x << ", " << vz.y << ", " << vz.z << ")");
    
    // 修复后应该是右手系
    REQUIRE(dot_z > 0.0f);
}

// 验证投影矩阵的 Z 方向
TEST_CASE("Projection Matrix Z Direction", "[math]") {
    float fovy = Math::to_radians(90.0f);
    float aspect = 16.0f / 9.0f;
    float near_plane = 0.1f;
    float far_plane = 100.0f;
    
    Mat4 proj = Math::perspective(fovy, aspect, near_plane, far_plane);
    
    // 对于左手系投影矩阵，Z 应该是 [0,1] 且 near < far
    // 对于右手系投影矩阵，Z 应该是 [0,1] 但方向相反
    
    // 检查 proj[2][2] 和 proj[3][2] 的符号
    INFO("proj[2][2] = " << proj.m[2][2]);
    INFO("proj[3][2] = " << proj.m[3][2]);
    
    // 标准 DX 左手投影: proj[2][2] = far/(far-near), proj[3][2] = -far*near/(far-near)
    REQUIRE(proj.m[3][2] < 0.0f);  // 应该是负数
}
