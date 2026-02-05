#include "engine/function/framework/component.h"
#include "engine/core/reflect/class_db.h"
#include "engine/core/reflect/serializer_registry.h" // Include the new registry
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
        std::string current_val_str = prop->getter(this);
        
        // Optimization: Don't save if equal to default
        if (current_val_str != prop->default_value_str) {
            // Try to find a typed serializer in the registry
            auto* serializer = SerializerRegistry::get().get_json_serializer(prop->type_index);
            if (serializer) {
                // Get value as std::any and serialize typed
                std::any val = prop->getter_any(this);
                (*serializer)(val, ar, prop->name);
            } else {
                ar(cereal::make_nvp(prop->name, current_val_str));
            }
        }
    }
}

void Component::serialize_modify(cereal::JSONInputArchive& ar) {
    std::string class_name(get_component_type_name());
    auto props = ClassDB::get().get_all_properties(class_name);

    for (const auto* prop : props) {
        try {
            // Try typed deserializer first
            auto* deserializer = SerializerRegistry::get().get_json_deserializer(prop->type_index);
            if (deserializer) {
                std::any val;
                (*deserializer)(val, ar, prop->name);
                prop->setter_any(this, val);
            } else {
                // Fallback: Read as string and use string setter
                std::string val_json;
                ar(cereal::make_nvp(prop->name, val_json));
                prop->setter(this, val_json);
            }
        } catch (...) {
            // Property not found in file -> keeps current value
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