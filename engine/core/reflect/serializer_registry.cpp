#include "serializer_registry.h"
#include "engine/core/math/math.h"
#include "engine/core/reflect/math_reflect.h"
#include <string>

SerializerRegistry& SerializerRegistry::get() {
    static SerializerRegistry instance;
    return instance;
}

SerializerRegistry::SerializerRegistry() {
    register_builtins();
}

void SerializerRegistry::register_builtins() {
    register_type<int>();
    register_type<float>();
    register_type<double>();
    register_type<bool>();
    register_type<std::string>();
    
    // Math types
    register_type<Vec2>();
    register_type<Vec3>();
    register_type<Vec4>();
    register_type<Quaternion>();
    register_type<Mat4>();
    
    // Transform
    register_type<Transform>();
}
