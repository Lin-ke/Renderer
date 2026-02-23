#include "class_db.h"
#include "engine/function/framework/component.h"
#include <string_view>
#include <string>

DEFINE_LOG_TAG(LogClassDB, "ClassDB");


ClassDB& ClassDB::get() {
    static ClassDB instance;
    return instance;
}


void ClassDB::register_class(std::string_view class_name, std::string_view parent_class_name, std::function<std::unique_ptr<Component>()> creator) {
    std::string cn(class_name);
    auto& info = classes_[cn];
    info.class_name = cn;
    if (!parent_class_name.empty()) {
        info.parent_class_name = std::string(parent_class_name);
    }
    if (creator) {
        info.creator = creator;
    }
}


std::unique_ptr<Component> ClassDB::create_component(std::string_view class_name) const {
    std::string cn(class_name);
    auto it = classes_.find(cn);
    if (it != classes_.end() && it->second.creator) {
        return it->second.creator();
    }
    LOG(ERROR) << "Failed to create component: " << class_name << " (Not registered or no creator)";
    return nullptr;
}


const ClassInfo* ClassDB::get_class_info(std::string_view class_name) const {
    std::string cn(class_name);
    auto it = classes_.find(cn);
    return it != classes_.end() ? &it->second : nullptr;
}


std::vector<const PropertyInfo*> ClassDB::get_all_properties(std::string_view class_name) const {
    std::vector<const PropertyInfo*> result;
    collect_properties_recursive(class_name, result);
    return result;
}

void ClassDB::collect_properties_recursive(std::string_view class_name, std::vector<const PropertyInfo*>& out) const {
    std::vector<const ClassInfo*> inheritance_chain;
    visit_class_chain(class_name, [&](const ClassInfo* info) {
        inheritance_chain.push_back(info);
        return false;
    });

    // Reverse order: Root -> Parent -> Child
    for (auto it = inheritance_chain.rbegin(); it != inheritance_chain.rend(); ++it) {
        const auto* info = *it;
        for (const auto& prop : info->properties) {
            out.push_back(&prop);
        }
    }
}

std::any ClassDB::get(Component* obj, std::string_view property_name) {
    if (!obj) return {};
    
    std::any result;
    std::string prop_name(property_name); 

    get().visit_class_chain(obj->get_component_type_name(), [&](const ClassInfo* info) -> bool {
        auto it = info->property_map.find(prop_name);
        if (it != info->property_map.end()) {
            result = info->properties[it->second].getter_any(obj);
            return true; 
        }
        return false; 
    });

    return result;
}
