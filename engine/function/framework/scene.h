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

        // 1. Serialize Prefab Entity to memory
        std::stringstream ss;
        {
            cereal::BinaryOutputArchive oarchive(ss);
            // We need to access the unique_ptr strictly for serialization if we want to serialize the pointer wrapper?
            // Wait, the previous code serialized the unique_ptr itself: oarchive(prefab->root_entity_);
            // The getter returns raw pointer. Cereal needs the unique_ptr to serialize it properly?
            // Actually, we are deserializing into a new unique_ptr.
            // If we serialize the raw pointer `*entity`, we need to deserialize into `*new_entity`.
            // Let's check how Cereal handles unique_ptr.
            // It serializes the pointee.
            // So if we serialize *prefab->get_root_entity(), we can deserialize into *new_entity (the object itself).
            
            // However, the previous code was `oarchive(prefab->root_entity_);`
            // This serializes the smart pointer wrapper (handles null state etc).
            // If I change to `oarchive(*prefab->get_root_entity())`, I skip the smart pointer wrapper.
            // But `instantiate` checks for null.
            
            // To maintain exact behavior, I might need friend access or a helper in Prefab.
            // Or just serialize the object content.
            
            oarchive(*prefab->get_root_entity());
        }

        // 2. Deserialize to new Entity
        auto new_entity = std::make_unique<Entity>();
        {
            cereal::BinaryInputArchive iarchive(ss);
            iarchive(*new_entity); // Deserialize into the object
        }

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
