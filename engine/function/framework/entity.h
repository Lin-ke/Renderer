#ifndef ENTITY_H
#define ENTITY_H
#include <memory>
#include "engine/function/framework/component.h"
#include "engine/function/asset/asset.h"
class Entity
{
    std::vector<ComponentPtr> components_;
    
public:
    template<typename T, typename... Args>
    T* add_component(Args&&... args) {
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = component.get();
        ptr->set_owner(this);
        components_.push_back(std::move(component));
        return ptr;
    }

    template<typename T>
    T* get_component() const {
        for (const auto& component : components_) {
            if (auto casted = dynamic_cast<T*>(component.get())) {
                return casted;
            }
        }
        return nullptr;
    }

    const std::vector<ComponentPtr>& get_components() const { return components_; }

    template<class Archive>
    void serialize(Archive &ar) {
        ar(cereal::make_nvp("components", components_));
    }

    void traverse_deps(std::function<void(std::shared_ptr<Asset>)> callback) const {
        for (const auto& comp : components_) {
            comp->traverse_deps(callback);
        }
    }

    void load_asset_deps() {
        for (auto& comp : components_) {
            comp->load_asset_deps();
        }
    }

    void save_asset_deps() {
        for (auto& comp : components_) {
            comp->save_asset_deps();
        }
    }

    // Deep clone using reflection
    std::unique_ptr<Entity> clone();
};

#endif