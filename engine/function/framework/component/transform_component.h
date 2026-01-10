#ifndef TRANSFORM_COMPONENT_H
#define TRANSFORM_COMPONENT_H
#include "engine/function/framework/component.h"
#include "engine/core/math/transform.h"
#include "engine/core/reflect/serialize.h"
#include "engine/core/reflect/math_reflect.h"
class TransformComponent : public Component {
public:
    Transform transform;

    friend class cereal::access;
    template<class Archive>
    void serialize(Archive &ar) {
        ar(cereal::make_nvp("transform", transform));
    }
};

CEREAL_REGISTER_TYPE(TransformComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, TransformComponent);
#endif