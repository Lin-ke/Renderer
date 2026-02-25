#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/core/math/math.h"
#include "engine/core/math/transform.h"
#include "engine/function/render/render_system/gizmo_manager.h"
#include <cmath>

// 验证坐标系的正确性
// 世界空间: X=右, Y=上, Z=前 (左手系)
TEST_CASE("View Matrix Coordinate System", "[math]") {
    // 相机在原点，朝向 +Z（世界前向）
    Vec3 eye(0, 0, 0);
    Vec3 center(0, 0, 1);  // 看向 +Z
    Vec3 up(0, 1, 0);      // Y 是上
    
    Mat4 view = Math::look_at(eye, center, up);
    
    // 验证：世界空间的基向量经过 View 变换后应该映射到观察空间的正确轴向
    
    // 世界 +Z(前) 应该映射到观察 +Z(前，相机前方)
    Vec4 world_front = Vec4(0, 0, 1, 1) * view;
    REQUIRE(std::abs(world_front.x) < 0.001f);  // X 分量应为 0
    REQUIRE(std::abs(world_front.y) < 0.001f);  // Y 分量应为 0
    REQUIRE(world_front.z > 0.9f);              // Z 分量应为正（相机前方）
    
    // 世界 +Y(上) 应该映射到观察 +Y(上)
    Vec4 world_up = Vec4(0, 1, 0, 1) * view;
    REQUIRE(std::abs(world_up.x) < 0.001f);
    REQUIRE(world_up.y > 0.9f);                 // Y 分量应为正
    REQUIRE(std::abs(world_up.z) < 0.001f);
    
    // 世界 +X(右) 应该映射到观察 +X(右)
    Vec4 world_right = Vec4(1, 0, 0, 1) * view;
    REQUIRE(world_right.x > 0.9f);              // X 分量应为正
    REQUIRE(std::abs(world_right.y) < 0.001f);
    REQUIRE(std::abs(world_right.z) < 0.001f);
}

// 验证左手系: right × up = front
TEST_CASE("World Basis Handedness", "[math]") {
    Vec3 right = Vec3::UnitX();
    Vec3 up = Vec3::UnitY();
    Vec3 front = Vec3::UnitZ();

    Vec3 cross_ru = right.cross(up);
    REQUIRE(cross_ru.dot(front) > 0.999f);
}

// 验证投影矩阵的 Z 方向
TEST_CASE("Projection Matrix Z Direction", "[math]") {
    float fovy = Math::to_radians(90.0f);
    float aspect = 16.0f / 9.0f;
    float near_plane = 0.1f;
    float far_plane = 100.0f;
    
    Mat4 proj = Math::perspective(fovy, aspect, near_plane, far_plane);
    
    Vec4 near_clip = Vec4(0.0f, 0.0f, near_plane, 1.0f) * proj;
    Vec4 far_clip = Vec4(0.0f, 0.0f, far_plane, 1.0f) * proj;
    float near_ndc_z = near_clip.z / near_clip.w;
    float far_ndc_z = far_clip.z / far_clip.w;

    REQUIRE(near_ndc_z == Catch::Approx(0.0f).margin(1e-3f));
    REQUIRE(far_ndc_z == Catch::Approx(1.0f).margin(1e-3f));
}

TEST_CASE("Transform Basis And RowMajor Translation", "[math][transform]") {
    Transform t;
    t.set_position(Vec3(3.0f, 4.0f, 5.0f));
    t.set_rotation(Vec3(0.0f, 0.0f, 0.0f));

    REQUIRE(t.right().dot(Vec3::UnitX()) > 0.999f);
    REQUIRE(t.up().dot(Vec3::UnitY()) > 0.999f);
    REQUIRE(t.front().dot(Vec3::UnitZ()) > 0.999f);

    Mat4 m = t.get_matrix();
    REQUIRE(m.m[3][0] == Catch::Approx(3.0f));
    REQUIRE(m.m[3][1] == Catch::Approx(4.0f));
    REQUIRE(m.m[3][2] == Catch::Approx(5.0f));
}

TEST_CASE("Gizmo RowMajor Matrix RoundTrip", "[math][gizmo]") {
    Mat4 m = Mat4::Identity();
    m.m[0][0] = 2.0f;
    m.m[1][1] = 3.0f;
    m.m[2][2] = 4.0f;
    m.m[3][0] = 10.0f;
    m.m[3][1] = 20.0f;
    m.m[3][2] = 30.0f;

    float arr[16] = {};
    GizmoManager::to_row_major_array(m, arr);

    REQUIRE(arr[12] == Catch::Approx(10.0f));
    REQUIRE(arr[13] == Catch::Approx(20.0f));
    REQUIRE(arr[14] == Catch::Approx(30.0f));

    Mat4 restored = GizmoManager::from_row_major_array(arr);
    REQUIRE(restored.m[3][0] == Catch::Approx(10.0f));
    REQUIRE(restored.m[3][1] == Catch::Approx(20.0f));
    REQUIRE(restored.m[3][2] == Catch::Approx(30.0f));
}
