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
     *        world = local * parent_world (row-major)
     */
    Mat4 get_world_matrix() const {
        Mat4 local = transform.get_matrix();
        Entity* owner = get_owner();
        if (!owner) return local;

        Entity* parent = owner->get_parent();
        if (!parent) return local;

        auto* parent_trans = parent->get_component<TransformComponent>();
        if (!parent_trans) return local;

        // For row-major matrices: world = local * parent_world
        return local * parent_trans->get_world_matrix();
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
        // In row-major, basis vectors are rows
        Vec3 rx = world.row(0).xyz();
        Vec3 ry = world.row(1).xyz();
        Vec3 rz = world.row(2).xyz();
        return Vec3(rx.length(), ry.length(), rz.length());
    }

    /**
     * @brief Get world-space rotation (extracted from world matrix)
     * Note: This returns a matrix-based rotation, more accurate for hierarchy
     */
    Quaternion get_world_rotation() const {
        Mat4 world = get_world_matrix();
        Vec3 scale = get_world_scale();
        
        // Extract and normalize rows
        Vec3 rx = world.row(0).xyz() / scale.x;
        Vec3 ry = world.row(1).xyz() / scale.y;
        Vec3 rz = world.row(2).xyz() / scale.z;
        
        DirectX::XMMATRIX rotMatrix(
            DirectX::XMVectorSet(rx.x, rx.y, rx.z, 0.0f),
            DirectX::XMVectorSet(ry.x, ry.y, ry.z, 0.0f),
            DirectX::XMVectorSet(rz.x, rz.y, rz.z, 0.0f),
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
