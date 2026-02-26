#pragma once

#include "engine/function/asset/asset.h"
#include "engine/function/framework/entity.h"
#include "engine/function/framework/prefab.h"
#include "engine/function/framework/component/prefab_component.h"
#include <memory>
#include <vector>
#include <cereal/archives/binary.hpp>
#include <sstream>

// Forward declarations for components
class CameraComponent;
class DirectionalLightComponent;
class PointLightComponent;
class VolumeLightComponent;

class Scene : public Asset {
public:
    std::vector<std::unique_ptr<Entity>> entities_;

    std::string_view get_asset_type_name() const override { return "Scene"; }
    AssetType get_asset_type() const override { return AssetType::Scene; }

    Entity* create_entity(const std::string& name = "") {
        auto entity = std::make_unique<Entity>();
        Entity* ptr = entity.get();
        ptr->set_name(name);
        entities_.push_back(std::move(entity));
        return ptr;
    }

    Entity* instantiate(std::shared_ptr<Prefab> prefab) {
        if (!prefab || !prefab->get_root_entity()) return nullptr;

        auto new_entity = prefab->get_root_entity()->clone();

        auto* prefab_comp = new_entity->get_component<PrefabComponent>();
        if (!prefab_comp) {
            prefab_comp = new_entity->add_component<PrefabComponent>();
        }
        prefab_comp->prefab = prefab;

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

    virtual void on_load() override {
        for (auto& entity : entities_) {
            if (!entity) continue;
            // Restore parent_ pointers lost during deserialization
            entity->restore_hierarchy();
            init_entity_recursive(entity.get());
        }
    }

    virtual void load_asset_deps() override {
        for (auto& entity : entities_) {
            setup_owner_recursive(entity.get());
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
            collect_components_recursive<TComponent>(entity.get(), components);
        }
        return components;
    }

    /**
     * @brief Get the first camera component in the scene
     * @return Pointer to camera, or nullptr if none exists
     */
    CameraComponent* get_camera() const;

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

private:
    // Recursively call on_init for all components in entity and its children
    static void init_entity_recursive(Entity* entity) {
        if (!entity) return;
        for (auto& comp : entity->get_components()) {
            if (comp) {
                comp->on_init();
            }
        }
        for (auto& child : entity->get_children()) {
            init_entity_recursive(child.get());
        }
    }

    // Recursively set owner for all components in entity and its children
    static void setup_owner_recursive(Entity* entity) {
        if (!entity) return;
        for (auto& comp : entity->get_components()) {
            if (comp) {
                comp->set_owner(entity);
            }
        }
        for (auto& child : entity->get_children()) {
            setup_owner_recursive(child.get());
        }
    }

    // Recursively collect components of a given type
    template<typename TComponent>
    static void collect_components_recursive(const Entity* entity,
                                             std::vector<TComponent*>& out) {
        if (!entity) return;
        if (auto* comp = entity->get_component<TComponent>()) {
            out.push_back(comp);
        }
        for (auto& child : entity->get_children()) {
            collect_components_recursive<TComponent>(child.get(), out);
        }
    }
};

CEREAL_REGISTER_TYPE(Scene);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, Scene);


