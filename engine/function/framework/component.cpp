#include "engine/function/framework/component.h"
#include "engine/core/reflect/class_db.h"
#include "engine/core/reflect/serializer_registry.h"
#include "engine/core/log/Log.h"

#include <cassert>

bool Component::set_property(const std::string& field_path, const std::string& value_json) {
    const auto* info = ClassDB::get().get_class_info(std::string(get_component_type_name()));
    assert(info && "Class not registered in ClassDB");

    auto it = info->property_map.find(field_path);
    assert(it != info->property_map.end() && "Property not found");
    return info->properties[it->second].setter(this, value_json);
}

std::string Component::get_property(const std::string& field_path) const {
    const auto* info = ClassDB::get().get_class_info(std::string(get_component_type_name()));
    assert(info && "Class not registered in ClassDB");

    auto it = info->property_map.find(field_path);
    assert(it != info->property_map.end() && "Property not found");
    return info->properties[it->second].getter(this);
}

void Component::serialize_modify(cereal::JSONOutputArchive& ar) {
    std::string class_name(get_component_type_name());
    auto props = ClassDB::get().get_all_properties(class_name);
    
    for (const auto* prop : props) {
        std::string current_val_str = prop->getter(this);
        
        if (current_val_str != prop->default_value_str) {
            auto* serializer = SerializerRegistry::get().get_json_serializer(prop->type_index);
            if (serializer) {
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
            auto* deserializer = SerializerRegistry::get().get_json_deserializer(prop->type_index);
            if (deserializer) {
                std::any val;
                (*deserializer)(val, ar, prop->name);
                prop->setter_any(this, val);
            } else {
                std::string val_json;
                ar(cereal::make_nvp(prop->name, val_json));
                prop->setter(this, val_json);
            }
        } catch (...) {
            // Property not found in file, keep current value
        }
    }
}

void Component::serialize_save(cereal::BinaryOutputArchive& ar) {
    std::string class_name(get_component_type_name());
    auto props = ClassDB::get().get_all_properties(class_name);

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