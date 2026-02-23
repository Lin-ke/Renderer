#pragma once

#include "engine/function/framework/component.h"
#include "engine/core/math/transform.h"
#include "engine/core/reflect/serialize.h"
#include "engine/core/reflect/math_reflect.h"
#include "engine/core/reflect/class_db.h"

class TransformComponent : public Component {
    CLASS_DEF(TransformComponent, Component)
public:
    virtual ~TransformComponent();

    Transform transform;

    static void register_class(){
        Registry::add<TransformComponent>("TransformComponent")
            .member("transform", &TransformComponent::transform);
    }
};

CEREAL_REGISTER_TYPE(TransformComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, TransformComponent);
