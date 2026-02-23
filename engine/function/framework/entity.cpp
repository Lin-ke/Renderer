#include "entity.h"
#include "engine/core/reflect/class_db.h"

#include <cassert>

std::unique_ptr<Entity> Entity::clone() {
    auto new_entity = std::make_unique<Entity>();

    for (const auto& comp : components_) {
        std::string type_name(comp->get_component_type_name());

        auto new_comp = ClassDB::get().create_component(type_name);
        assert(new_comp && "Failed to create component via ClassDB");

        auto properties = ClassDB::get().get_all_properties(type_name);
        for (const auto* prop : properties) {
            std::any val = prop->getter_any(comp.get());
            if (val.has_value()) {
                prop->setter_any(new_comp.get(), val);
            }
        }

        new_comp->set_owner(new_entity.get());
        new_entity->components_.push_back(std::move(new_comp));
    }

    return new_entity;
}
