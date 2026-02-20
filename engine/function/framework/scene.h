#ifndef SCENE_H
#define SCENE_H
#include "engine/function/asset/asset.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/prefab.h"
#include "engine/function/framework/component/prefab_component.h"
#include <memory>
#include <vector>
#include <cereal/archives/binary.hpp>
#include <sstream>

// Forward declarations for light components
class DirectionalLightComponent;
class PointLightComponent;
class VolumeLightComponent;

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

    /**
     * @brief Get all components of a specific type from all entities in the scene
     * @tparam TComponent The component type to search for
     * @return Vector of pointers to found components
     */
    template<typename TComponent>
    std::vector<TComponent*> get_components() const {
        std::vector<TComponent*> components;
        for (const auto& entity : entities_) {
            if (auto* comp = entity->get_component<TComponent>()) {
                components.push_back(comp);
            }
        }
        return components;
    }

    /**
     * @brief Get the first directional light component in the scene
     * @return Pointer to directional light, or nullptr if none exists
     */
    DirectionalLightComponent* get_directional_light() const;

    /**
     * @brief Get all point light components in the scene
     * @return Vector of point light component pointers
     */
    std::vector<PointLightComponent*> get_point_lights() const {
        return get_components<PointLightComponent>();
    }

    /**
     * @brief Get all volume light components in the scene
     * @return Vector of volume light component pointers
     */
    std::vector<VolumeLightComponent*> get_volume_lights() const {
        return get_components<VolumeLightComponent>();
    }

    /**
     * @brief Tick all entities in the scene
     * @param delta_time Time since last frame in seconds
     */
    void tick(float delta_time) {
        for (auto& entity : entities_) {
            if (entity) {
                entity->tick(delta_time);
            }
        }
    }
    
};

CEREAL_REGISTER_TYPE(Scene);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, Scene);

#endif
