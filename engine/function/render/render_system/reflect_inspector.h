#pragma once
#include "engine/function/framework/component.h"
#include "engine/core/math/math.h"
#include "engine/core/math/transform.h"
#include <imgui.h>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <any>
#include <string>

/**
 * @brief Property drawer registry for ImGui reflection inspector
 * 
 * Registers custom property drawers for different types
 */
class ReflectInspector {
public:
    using PropertyDrawer = std::function<void(Component* component, const std::string& property_name, 
                                               const std::type_info& type, 
                                               std::function<std::any()> getter,
                                               std::function<void(const std::any&)> setter)>;

    static ReflectInspector& get();

    /**
     * @brief Draw all properties of a component using reflection
     */
    void draw_component(Component* component);

    /**
     * @brief Register a custom property drawer for a specific type
     */
    template<typename T>
    void register_drawer(std::function<void(const std::string& name, T& value)> drawer) {
        drawers_[typeid(T)] = [drawer](Component* comp, const std::string& name, 
                                       const std::type_info& type,
                                       std::function<std::any()> getter,
                                       std::function<void(const std::any&)> setter) {
            (void)comp;
            (void)type;
            // This won't work directly - need different approach
        };
    }

    /**
     * @brief Initialize default property drawers
     */
    void init_default_drawers();

private:
    ReflectInspector() = default;
    
    // Type-specific drawers
    std::unordered_map<std::type_index, PropertyDrawer> drawers_;
    
    // Default drawer for unknown types
    void draw_unknown(const std::string& name, const std::type_info& type);
    
    // Built-in type drawers
    static void draw_float(const std::string& name, float& value);
    static void draw_int(const std::string& name, int& value);
    static void draw_bool(const std::string& name, bool& value);
    static void draw_vec2(const std::string& name, Vec2& value);
    static void draw_vec3(const std::string& name, Vec3& value);
    static void draw_vec4(const std::string& name, Vec4& value);
    static void draw_string(const std::string& name, std::string& value);
    
    // Helper to get/set values via string serialization
    bool edit_property_string(const std::string& name, const std::type_info& type,
                              const std::string& current_value,
                              std::string& out_new_value);
};
