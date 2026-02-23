#include "engine/function/framework/component/prefab_component.h"
#include "engine/function/framework/entity.h"
#include "engine/core/log/Log.h"
#include "engine/core/reflect/class_db.h"

REGISTER_CLASS_IMPL(PrefabComponent)

void PrefabComponent::generate_modifications() {
    if (!owner_ || !prefab || !prefab->get_root_entity()) return;

    modifications.clear();
    Entity* instance = owner_;
    Entity* prefab_root = prefab->get_root_entity();

    auto& instance_comps = instance->get_components();
    auto& prefab_comps = prefab_root->get_components();


    for (const auto& inst_comp : instance_comps) {
        // Skip PrefabComponent itself
        if (inst_comp.get() == this) continue;

        std::string type_name(inst_comp->get_component_type_name());
        
        // Find matching component in prefab
        Component* prefab_match = nullptr;
        for (const auto& p_comp : prefab_comps) {
            if (p_comp->get_component_type_name() == type_name) {
                prefab_match = p_comp.get();
                break;
            }
        }

        if (prefab_match) {
            auto props = ClassDB::get().get_all_properties(type_name);
            for (const auto* prop : props) {
                std::string val_inst = prop->getter(inst_comp.get());
                std::string val_prefab = prop->getter(prefab_match);
                if (val_inst != val_prefab) {
                    modifications.push_back({type_name, prop->name, val_inst});
                }
            }
        }
    }
}

void PrefabComponent::apply_modifications(Entity* root_entity) {
    if (!root_entity) return;

    for (const auto& mod : modifications) {
        bool found = false;
        for (const auto& comp : root_entity->get_components()) {
            if (comp->get_component_type_name() == mod.target_component) {
                if (comp->set_property(mod.field_path, mod.value)) {
                    found = true;
                } else {
                    WARN(LogAsset, "Failed to set property {} on component {}", mod.field_path, mod.target_component);
                }
                break;
            }
        }
        if (!found) {
            WARN(LogAsset, "Component {} not found for modification", mod.target_component);
        }
    }
}