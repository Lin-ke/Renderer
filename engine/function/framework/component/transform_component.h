#pragma once

#include "engine/function/framework/component.h"
#include "engine/function/framework/entity.h"
#include "engine/core/math/transform.h"
#include "engine/core/reflect/serialize.h"
#include "engine/core/reflect/math_reflect.h"
#include "engine/core/reflect/class_db.h"

class TransformComponent : public Component {
    CLASS_DEF(TransformComponent, Component)
public:
    virtual ~TransformComponent();

    Transform transform;

    /**
     * @brief Get the world-space matrix by walking up the parent chain.
     *        world = parent_world * local
     */
    Mat4 get_world_matrix() const {
        Mat4 local = transform.get_matrix();
        Entity* owner = get_owner();
        if (!owner) return local;

        Entity* parent = owner->get_parent();
        if (!parent) return local;

        auto* parent_trans = parent->get_component<TransformComponent>();
        if (!parent_trans) return local;

        return parent_trans->get_world_matrix() * local;
    }

    /**
     * @brief Get world-space position (extracted from world matrix)
     */
     // 对于行向量 v * M 的约定，平移分量确实存储在第3行
    Vec3 get_world_position() const {
        Vec4 row3 = get_world_matrix().row(3);
        return Vec3(row3.x, row3.y, row3.z);
    }
    /**
     * @brief Get world-space scale (decomposed from world matrix)
     * Note: This assumes uniform scale or no skew in parent chain
     */
    Vec3 get_world_scale() const {
        Mat4 world = get_world_matrix();
        Vec3 cx = Vec3(world.m[0][0], world.m[1][0], world.m[2][0]);
        Vec3 cy = Vec3(world.m[0][1], world.m[1][1], world.m[2][1]);
        Vec3 cz = Vec3(world.m[0][2], world.m[1][2], world.m[2][2]);
        return Vec3(cx.length(), cy.length(), cz.length());
    }

    /**
     * @brief Get world-space rotation (extracted from world matrix)
     * Note: This returns a matrix-based rotation, more accurate for hierarchy
     */
    Quaternion get_world_rotation() const {
        Mat4 world = get_world_matrix();
        // Extract rotation matrix columns and normalize (remove scale)
        Vec3 cx = Vec3(world.m[0][0], world.m[1][0], world.m[2][0]).normalized();
        Vec3 cy = Vec3(world.m[0][1], world.m[1][1], world.m[2][1]).normalized();
        Vec3 cz = Vec3(world.m[0][2], world.m[1][2], world.m[2][2]).normalized();
        
        // Convert rotation matrix to quaternion using DirectXMath
        DirectX::XMMATRIX rotMatrix(
            DirectX::XMVectorSet(cx.x, cy.x, cz.x, 0.0f),
            DirectX::XMVectorSet(cx.y, cy.y, cz.y, 0.0f),
            DirectX::XMVectorSet(cx.z, cy.z, cz.z, 0.0f),
            DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f)
        );
        return Quaternion(DirectX::XMQuaternionRotationMatrix(rotMatrix));
    }

    static void register_class(){
        Registry::add<TransformComponent>("TransformComponent")
            .member("transform", &TransformComponent::transform);
    }
};

CEREAL_REGISTER_TYPE(TransformComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, TransformComponent);
