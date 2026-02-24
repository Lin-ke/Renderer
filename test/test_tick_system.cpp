#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include "test/test_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/framework/world.h"
#include "engine/function/framework/scene.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/component/transform_component.h"
#include "engine/function/framework/component.h"
#include "engine/core/math/math.h"
#include "engine/core/reflect/class_db.h"

// 定义 M_PI（如果未定义）
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

DEFINE_LOG_TAG(LogTestTick, "TestTick");

using Catch::Approx;

// ============================================================================
// OrbitComponent - 让实体围绕中心点做圆形轨道运动
// ============================================================================
class OrbitComponent : public Component {
    CLASS_DEF(OrbitComponent, Component)
public:
    OrbitComponent() = default;
    
    // 设置轨道参数
    void set_orbit_params(const Vec3& center, float radius, float angular_speed) {
        center_ = center;
        radius_ = radius;
        angular_speed_ = angular_speed;  // 弧度/秒
    }
    
    // 设置初始角度（弧度）
    void set_initial_angle(float angle) {
        current_angle_ = angle;
    }
    
    // 每帧更新位置
    virtual void on_update(float delta_time) override {
        // 更新当前角度
        current_angle_ += angular_speed_ * delta_time;
        
        // 保持角度在 0 ~ 2π 范围内
        while (current_angle_ > 2.0f * M_PI) {
            current_angle_ -= 2.0f * M_PI;
        }
        
        // 计算新的位置：x = center.x + radius * cos(angle), z = center.z + radius * sin(angle)
        Vec3 new_position;
        new_position.x() = center_.x() + radius_ * std::cos(current_angle_);
        new_position.y() = center_.y();  // 保持 y 不变（在 xz 平面运动）
        new_position.z() = center_.z() + radius_ * std::sin(current_angle_);
        
        // 更新 TransformComponent 的位置
        if (auto* transform = get_owner()->get_component<TransformComponent>()) {
            transform->transform.set_position(new_position);
        }
    }
    
    // 获取当前角度
    float get_current_angle() const { return current_angle_; }
    
    // 获取当前位置（计算得出）
    Vec3 get_current_position() const {
        Vec3 pos;
        pos.x() = center_.x() + radius_ * std::cos(current_angle_);
        pos.y() = center_.y();
        pos.z() = center_.z() + radius_ * std::sin(current_angle_);
        return pos;
    }
    
    // 获取目标位置（经过 delta_time 后的位置）
    Vec3 get_expected_position(float delta_time) const {
        float future_angle = current_angle_ + angular_speed_ * delta_time;
        Vec3 pos;
        pos.x() = center_.x() + radius_ * std::cos(future_angle);
        pos.y() = center_.y();
        pos.z() = center_.z() + radius_ * std::sin(future_angle);
        return pos;
    }
    
    static void register_class() {
        Registry::add<OrbitComponent>("OrbitComponent")
            .member("radius", &OrbitComponent::radius_)
            .member("angular_speed", &OrbitComponent::angular_speed_);
    }

private:
    Vec3 center_ = Vec3::Zero();    // 轨道中心
    float radius_ = 10.0f;           // 轨道半径
    float angular_speed_ = 1.0f;     // 角速度（弧度/秒）
    float current_angle_ = 0.0f;     // 当前角度（弧度）
};

CEREAL_REGISTER_TYPE(OrbitComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, OrbitComponent);

// ============================================================================
// 测试用例：Tick 系统基础功能
// ============================================================================
TEST_CASE("Tick system basic functionality", "[tick]") {
    test_utils::TestContext::reset();
    
    // 获取 World 并创建场景
    World* world = EngineContext::world();
    REQUIRE(world != nullptr);
    
    auto scene = std::make_shared<Scene>();
    world->set_active_scene(scene);
    
    SECTION("Entity tick propagates to components") {
        // 创建飞船实体
        Entity* ship = scene->create_entity();
        REQUIRE(ship != nullptr);
        
        // 添加 Transform 组件
        auto* transform = ship->add_component<TransformComponent>();
        REQUIRE(transform != nullptr);
        transform->transform.set_position(Vec3(10.0f, 0.0f, 0.0f));
        
        // 添加轨道运动组件
        auto* orbit = ship->add_component<OrbitComponent>();
        REQUIRE(orbit != nullptr);
        orbit->set_orbit_params(Vec3::Zero(), 10.0f, 1.0f);  // 围绕原点，半径10，速度1弧度/秒
        orbit->set_initial_angle(0.0f);  // 从 x 轴正方向开始
        
        // 验证初始位置
        REQUIRE(transform->transform.get_position().x() == Approx(10.0f));
        REQUIRE(transform->transform.get_position().z() == Approx(0.0f));
        
        // 模拟一帧：delta_time = 1.0 秒
        ship->tick(1.0f);
        
        // 验证位置：1秒后，角度应该是1弧度
        float expected_x = 10.0f * std::cos(1.0f);
        float expected_z = 10.0f * std::sin(1.0f);
        
        REQUIRE(transform->transform.get_position().x() == Approx(expected_x).epsilon(0.001));
        REQUIRE(transform->transform.get_position().z() == Approx(expected_z).epsilon(0.001));
        
        // 验证 OrbitComponent 的角度更新
        REQUIRE(orbit->get_current_angle() == Approx(1.0f).epsilon(0.001));
    }
    
    SECTION("Multiple ticks accumulate correctly") {
        // 创建飞船
        Entity* ship = scene->create_entity();
        auto* transform = ship->add_component<TransformComponent>();
        auto* orbit = ship->add_component<OrbitComponent>();
        
        orbit->set_orbit_params(Vec3::Zero(), 5.0f, 2.0f);  // 半径5，速度2弧度/秒
        orbit->set_initial_angle(0.0f);
        
        // 模拟10帧，每帧0.1秒
        for (int i = 0; i < 10; ++i) {
            ship->tick(0.1f);
        }
        
        // 总时间1秒，角度应该是2弧度
        float expected_angle = 2.0f;  // 2弧度/秒 * 1秒
        float expected_x = 5.0f * std::cos(expected_angle);
        float expected_z = 5.0f * std::sin(expected_angle);
        
        REQUIRE(orbit->get_current_angle() == Approx(expected_angle).epsilon(0.001));
        REQUIRE(transform->transform.get_position().x() == Approx(expected_x).epsilon(0.001));
        REQUIRE(transform->transform.get_position().z() == Approx(expected_z).epsilon(0.001));
    }
    
    test_utils::TestContext::reset();
}

// ============================================================================
// 测试用例：World -> Scene -> Entity -> Component Tick 传播
// ============================================================================
TEST_CASE("Tick propagation through hierarchy", "[tick]") {
    test_utils::TestContext::reset();
    
    World* world = EngineContext::world();
    auto scene = std::make_shared<Scene>();
    world->set_active_scene(scene);
    
    SECTION("World tick updates all entities") {
        // 创建多艘飞船
        std::vector<Entity*> ships;
        std::vector<OrbitComponent*> orbits;
        
        for (int i = 0; i < 3; ++i) {
            Entity* ship = scene->create_entity();
            ship->add_component<TransformComponent>();
            auto* orbit = ship->add_component<OrbitComponent>();
            
            // 不同的轨道参数
            orbit->set_orbit_params(Vec3::Zero(), 10.0f + i * 5.0f, 0.5f + i * 0.5f);
            orbit->set_initial_angle(i * 2.0f * M_PI / 3.0f);  // 均匀分布在圆周上
            
            ships.push_back(ship);
            orbits.push_back(orbit);
        }
        
        // 记录 tick 前的角度
        std::vector<float> angles_before;
        for (auto* orbit : orbits) {
            angles_before.push_back(orbit->get_current_angle());
        }
        
        // 通过 World 进行 tick
        world->tick(1.0f);  // 1秒
        
        // 验证所有飞船都更新了
        for (size_t i = 0; i < orbits.size(); ++i) {
            float expected_speed = 0.5f + i * 0.5f;
            float expected_angle = angles_before[i] + expected_speed * 1.0f;
            
            // 处理角度归一化
            while (expected_angle > 2.0f * M_PI) {
                expected_angle -= 2.0f * M_PI;
            }
            
            REQUIRE(orbits[i]->get_current_angle() == Approx(expected_angle).epsilon(0.001));
        }
    }
    
    test_utils::TestContext::reset();
}

// ============================================================================
// 测试用例：帧率无关的运动（关键测试）
// ============================================================================
TEST_CASE("Frame rate independent movement", "[tick]") {
    test_utils::TestContext::reset();
    
    World* world = EngineContext::world();
    auto scene = std::make_shared<Scene>();
    world->set_active_scene(scene);
    
    SECTION("Same total time produces same result regardless of frame count") {
        // 创建两个相同的飞船
        Entity* ship1 = scene->create_entity();
        ship1->add_component<TransformComponent>();
        auto* orbit1 = ship1->add_component<OrbitComponent>();
        orbit1->set_orbit_params(Vec3::Zero(), 10.0f, 1.0f);  // 1弧度/秒
        orbit1->set_initial_angle(0.0f);
        
        Entity* ship2 = scene->create_entity();
        ship2->add_component<TransformComponent>();
        auto* orbit2 = ship2->add_component<OrbitComponent>();
        orbit2->set_orbit_params(Vec3::Zero(), 10.0f, 1.0f);
        orbit2->set_initial_angle(0.0f);
        
        // 飞船1：10帧，每帧0.1秒（共1秒）
        for (int i = 0; i < 10; ++i) {
            ship1->tick(0.1f);
        }
        
        // 飞船2：1帧，1秒
        ship2->tick(1.0f);
        
        // 两者应该到达相同位置
        auto* transform1 = ship1->get_component<TransformComponent>();
        auto* transform2 = ship2->get_component<TransformComponent>();
        
        REQUIRE(transform1->transform.get_position().x() == 
                Approx(transform2->transform.get_position().x()).epsilon(0.001));
        REQUIRE(transform1->transform.get_position().z() == 
                Approx(transform2->transform.get_position().z()).epsilon(0.001));
        
        REQUIRE(orbit1->get_current_angle() == 
                Approx(orbit2->get_current_angle()).epsilon(0.001));
    }
    
    SECTION("Complete orbit returns to start position") {
        Entity* ship = scene->create_entity();
        ship->add_component<TransformComponent>();
        auto* orbit = ship->add_component<OrbitComponent>();
        
        orbit->set_orbit_params(Vec3(0.0f, 5.0f, 0.0f), 8.0f, M_PI);  // 角速度 π 弧度/秒，2秒一圈
        orbit->set_initial_angle(0.0f);
        
        // 记录起始位置
        Vec3 start_pos = orbit->get_current_position();
        
        // 模拟2秒（完整一圈）
        ship->tick(2.0f);
        
        // 应该回到接近起点（角度归一化会导致微小差异）
        Vec3 end_pos = orbit->get_current_position();
        
        REQUIRE(end_pos.x() == Approx(start_pos.x()).margin(0.001));
        REQUIRE(end_pos.z() == Approx(start_pos.z()).margin(0.001));
        REQUIRE(orbit->get_current_angle() == Approx(0.0f).margin(0.001));
    }
    
    test_utils::TestContext::reset();
}

// ============================================================================
// 测试用例：DeltaTime 查询
// ============================================================================
TEST_CASE("EngineContext time queries", "[tick]") {
    test_utils::TestContext::reset();
    
    SECTION("Initial tick count is zero") {
        REQUIRE(EngineContext::get_current_tick() == 0);
        REQUIRE(EngineContext::get_delta_time() == 0.0f);
    }
    
    test_utils::TestContext::reset();
}
