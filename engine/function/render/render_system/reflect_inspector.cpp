#include "reflect_inspector.h"
#include "engine/core/reflect/class_db.h"
#include "engine/core/log/Log.h"
#include <sstream>

DEFINE_LOG_TAG(LogReflectInspector, "ReflectInspector");

ReflectInspector& ReflectInspector::get() {
    static ReflectInspector instance;
    static bool initialized = false;
    if (!initialized) {
        instance.init_default_drawers();
        initialized = true;
    }
    return instance;
}

void ReflectInspector::init_default_drawers() {
    // Register default drawers for common types
    // Using string-based serialization for simplicity
}

void ReflectInspector::draw_component(Component* component) {
    if (!component) return;
    
    std::string class_name(component->get_component_type_name());
    const ClassInfo* class_info = ClassDB::get().get_class_info(class_name);
    if (!class_info) {
        ImGui::Text("No reflection info for: %s", class_name.c_str());
        return;
    }
    
    // Get all properties including inherited
    auto properties = ClassDB::get().get_all_properties(class_name);
    
    for (const PropertyInfo* prop : properties) {
        if (!prop) continue;
        
        // Use any-based getters/setters for direct type access
        bool edited = false;
        std::any current_any = prop->getter_any(component);
        
        if (prop->type_index == typeid(float)) {
            float val = std::any_cast<float>(current_any);
            if (ImGui::DragFloat(prop->name.c_str(), &val, 0.1f)) {
                prop->setter_any(component, std::any(val));
            }
        }
        else if (prop->type_index == typeid(int)) {
            int val = std::any_cast<int>(current_any);
            if (ImGui::DragInt(prop->name.c_str(), &val)) {
                prop->setter_any(component, std::any(val));
            }
        }
        else if (prop->type_index == typeid(bool)) {
            bool val = std::any_cast<bool>(current_any);
            if (ImGui::Checkbox(prop->name.c_str(), &val)) {
                prop->setter_any(component, std::any(val));
            }
        }
        else if (prop->type_index == typeid(Vec2)) {
            Vec2 val = std::any_cast<Vec2>(current_any);
            if (ImGui::DragFloat2(prop->name.c_str(), &val.x, 0.1f)) {
                prop->setter_any(component, std::any(val));
            }
        }
        else if (prop->type_index == typeid(Vec3)) {
            Vec3 val = std::any_cast<Vec3>(current_any);
            if (ImGui::DragFloat3(prop->name.c_str(), &val.x, 0.1f)) {
                prop->setter_any(component, std::any(val));
            }
        }
        else if (prop->type_index == typeid(Vec4)) {
            Vec4 val = std::any_cast<Vec4>(current_any);
            if (ImGui::DragFloat4(prop->name.c_str(), &val.x, 0.1f)) {
                prop->setter_any(component, std::any(val));
            }
        }
        else if (prop->type_index == typeid(std::string)) {
            std::string val = std::any_cast<std::string>(current_any);
            char buf[256] = {};
            strncpy(buf, val.c_str(), sizeof(buf) - 1);
            if (ImGui::InputText(prop->name.c_str(), buf, sizeof(buf))) {
                prop->setter_any(component, std::any(std::string(buf)));
            }
        }
        else if (prop->type_index == typeid(Transform)) {
            // Special handling for Transform - show as nested fields
            Transform val = std::any_cast<Transform>(current_any);
            
            Vec3 position = val.get_position();
            Vec3 rotation = val.get_euler_angle();
            Vec3 scale = val.get_scale();
            
            bool transform_edited = false;
            
            ImGui::PushID(prop->name.c_str());
            if (ImGui::DragFloat3("Position", &position.x, 0.1f)) transform_edited = true;
            if (ImGui::DragFloat3("Rotation", &rotation.x, 1.0f)) transform_edited = true;
            if (ImGui::DragFloat3("Scale", &scale.x, 0.1f)) transform_edited = true;
            ImGui::PopID();
            
            if (transform_edited) {
                val.set_position(position);
                val.set_rotation(rotation);
                val.set_scale(scale);
                prop->setter_any(component, std::any(val));
            }
        }
        else {
            // Unknown type - show as read-only text using string serialization
            std::string val = prop->getter(component);
            ImGui::LabelText(prop->name.c_str(), "%s", val.c_str());
        }
    }
}

bool ReflectInspector::edit_property_string(const std::string& name, const std::type_info& type,
                                             const std::string& current_value,
                                             std::string& out_new_value) {
    (void)name;
    (void)type;
    (void)current_value;
    out_new_value = current_value;
    return false;
}
