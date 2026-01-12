#pragma once
#include "engine/function/asset/asset.h"
#include "engine/function/framework/entity.h"
#include <cereal/archives/binary.hpp>

class Prefab : public Asset {
    std::unique_ptr<Entity> root_entity_;

public:
    std::string_view get_asset_type_name() const override { return "Prefab"; }
    AssetType get_asset_type() const override { return AssetType::Prefab; }

    Entity* get_root_entity() const { return root_entity_.get(); }
    void set_root_entity(std::unique_ptr<Entity> entity) { root_entity_ = std::move(entity); }

    friend class cereal::access;
    template <class Archive>
    void serialize(Archive &ar) {
        ar(cereal::base_class<Asset>(this));
        ar(cereal::make_nvp("root_entity", root_entity_));
    }

    void traverse_deps(std::function<void(std::shared_ptr<Asset>)> callback) const override {
        if (root_entity_) {
            root_entity_->traverse_deps(callback);
        }
    }
};

CEREAL_REGISTER_TYPE(Prefab);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, Prefab);
