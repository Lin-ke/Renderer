#include "entity.h"
#include "engine/core/reflect/class_db.h"

std::unique_ptr<Entity> Entity::clone() {
    auto new_entity = std::make_unique<Entity>();

    for (const auto& comp : components_) {
        // 1. Get Component Type Name
        std::string type_name(comp->get_component_type_name());

        // 2. Create new Component instance via ClassDB
        auto new_comp = ClassDB::get().create_component(type_name);
        if (!new_comp) {
            continue; 
        }

        // 3. Copy Properties via Reflection
        // Iterate all properties in inheritance chain
        auto properties = ClassDB::get().get_all_properties(type_name);
        for (const auto* prop : properties) {
            // Get value from source (as std::any)
            std::any val = prop->getter_any(comp.get());
            // Set value to target
            if (val.has_value()) {
                prop->setter_any(new_comp.get(), val);
            }
        }

        // 4. Attach to new Entity
        new_comp->set_owner(new_entity.get());
        new_entity->components_.push_back(std::move(new_comp));
    }

    return new_entity;
}
