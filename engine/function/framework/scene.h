#ifndef SCENE_H
#define SCENE_H
#include "engine/function/asset/asset.h"
#include "engine/function/framework/entity.h"
#include <memory>
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

    friend class cereal::access;

    template <class Archive>
    void serialize(Archive &ar) {
        ar(cereal::base_class<Asset>(this));
        ar(cereal::make_nvp("entities", entities_));
    }

    virtual std::vector<std::shared_ptr<Asset>> get_deps() const override {
        std::vector<std::shared_ptr<Asset>> deps;
        for (const auto& entity : entities_) {
            auto entity_deps = entity->get_deps();
            deps.insert(deps.end(), entity_deps.begin(), entity_deps.end());
        }
        return deps;
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
