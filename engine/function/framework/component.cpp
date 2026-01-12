#include "engine/function/framework/component.h"
#include "engine/core/reflect/class_db.h"
#include "engine/core/log/Log.h"

bool Component::set_property(const std::string& field_path, const std::string& value_json) {
    const auto* info = ClassDB::get().get_class_info(std::string(get_component_type_name()));
    if (!info) return false;

    auto it = info->property_map.find(field_path);
    if (it != info->property_map.end()) {
        return info->properties[it->second].setter(this, value_json);
    }
    return false;
}

std::string Component::get_property(const std::string& field_path) const {
    const auto* info = ClassDB::get().get_class_info(std::string(get_component_type_name()));
    if (!info) return "";

    auto it = info->property_map.find(field_path);
    if (it != info->property_map.end()) {
        return info->properties[it->second].getter(this);
    }
    return "";
}

void Component::serialize_modify(cereal::JSONOutputArchive& ar) {
    std::string class_name(get_component_type_name());
    auto props = ClassDB::get().get_all_properties(class_name);
    
    for (const auto* prop : props) {
        std::string current_val = prop->getter(this);
        // INFO("Asset", "  Prop {}: Val='{}' Default='{}'", prop->name, current_val, prop->default_value_str);
        
        // Optimization: Don't save if equal to default
        if (current_val != prop->default_value_str) {
            ar(cereal::make_nvp(prop->name, current_val));
        }
    }
}

void Component::serialize_modify(cereal::JSONInputArchive& ar) {
    std::string class_name(get_component_type_name());
    auto props = ClassDB::get().get_all_properties(class_name);

    for (const auto* prop : props) {
        try {
            std::string val_json;
            // Attempt to read the property as a string
            ar(cereal::make_nvp(prop->name, val_json));
            // Apply it
            prop->setter(this, val_json);
        } catch (...) {
            // Property not found in file -> keeps current value (which is default initialized)
        }
    }
}

void Component::serialize_save(cereal::BinaryOutputArchive& ar) {
    std::string class_name(get_component_type_name());
    auto props = ClassDB::get().get_all_properties(class_name);

    // In binary, we cannot skip fields reliably without complex schemes.
    // So we just write all properties in order.
    for (const auto* prop : props) {
        std::string current_val = prop->getter(this);
        ar(current_val);
    }
}

void Component::serialize_save(cereal::BinaryInputArchive& ar) {
    std::string class_name(get_component_type_name());
    auto props = ClassDB::get().get_all_properties(class_name);

    for (const auto* prop : props) {
        std::string val_json;
        ar(val_json);
        prop->setter(this, val_json);
    }
}