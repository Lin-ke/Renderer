#pragma once
#include "engine/function/framework/component.h"
#include "engine/function/framework/prefab.h"
#include "engine/function/asset/asset_macros.h"

class PrefabComponent : public Component {
    CLASS_DEF(PrefabComponent, Component)
public:
    struct Modification {
        std::string target_component; // e.g., "HealthComponent"
        std::string field_path;       // e.g., "max_hp"
        std::string value;            // Stored as string, manually parsed by set_property

        template <class Archive>
        void serialize(Archive &ar) {
            ar(cereal::make_nvp("target_component", target_component),
               cereal::make_nvp("field_path", field_path),
               cereal::make_nvp("value", value));
        }
    };

    std::vector<Modification> modifications;

    ASSET_DEPS(
        (std::shared_ptr<Prefab>, prefab)
    )

    template <class Archive>
    void serialize(Archive &ar) {
        if constexpr (Archive::is_saving::value) {
            generate_modifications();
        }
        serialize_deps(ar);
        ar(cereal::make_nvp("modifications", modifications));
    }

    void apply_modifications(class Entity* root_entity);
    void generate_modifications();
};

CEREAL_REGISTER_TYPE(PrefabComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, PrefabComponent);
