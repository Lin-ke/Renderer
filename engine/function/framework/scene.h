#ifndef SCENE_H
#define SCENE_H
#include "engine/function/asset/asset.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/prefab.h"
#include "engine/function/framework/component/prefab_component.h"
#include <memory>
#include <cereal/archives/binary.hpp>
#include <sstream>

class Scene : public Asset, public std::enable_shared_from_this<Scene> {
public:
    std::vector<std::unique_ptr<Entity>> entities_;

    std::string_view get_asset_type_name() const override { return "Scene"; }
    AssetType get_asset_type() const override { return AssetType::Scene; }

    Entity* create_entity() {
        auto entity = std::make_unique<Entity>();
        Entity* ptr = entity.get();
        entities_.push_back(std::move(entity));
        return ptr;
    }

    Entity* instantiate(std::shared_ptr<Prefab> prefab) {
        if (!prefab || !prefab->get_root_entity()) return nullptr;

        // Use efficient clone via reflection
        auto new_entity = prefab->get_root_entity()->clone();

        // 3. Attach PrefabComponent
        auto* prefab_comp = new_entity->get_component<PrefabComponent>();
        if (!prefab_comp) {
            prefab_comp = new_entity->add_component<PrefabComponent>();
        }
        prefab_comp->prefab = prefab;

        // 4. Add to Scene
        Entity* ptr = new_entity.get();
        entities_.push_back(std::move(new_entity));
        return ptr;
    }

    friend class cereal::access;

    template <class Archive>
    void serialize(Archive &ar) {
        ar(cereal::base_class<Asset>(this));
        ar(cereal::make_nvp("entities", entities_));
    }

    virtual void traverse_deps(std::function<void(std::shared_ptr<Asset>)> callback) const override {
        for (const auto& entity : entities_) {
            entity->traverse_deps(callback);
        }
    }

    virtual void load_asset_deps() override {
        for (auto& entity : entities_) {
            // Fix component owner pointers after deserialization
            for (auto& comp : entity->get_components()) {
                if (comp) {
                    comp->set_owner(entity.get());
                }
            }
            entity->load_asset_deps();
        }
    }

    virtual void save_asset_deps() override {
        for (auto& entity : entities_) {
            entity->save_asset_deps();
        }
    }
    
};

CEREAL_REGISTER_TYPE(Scene);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, Scene);

#endif
