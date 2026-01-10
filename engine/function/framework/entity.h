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

    template<class Archive>
    void serialize(Archive &ar) {
        ar(cereal::make_nvp("components", components_));
    }

    std::vector<std::shared_ptr<Asset>> get_deps() const {
        std::vector<std::shared_ptr<Asset>> deps;
        for (const auto& comp : components_) {
            auto comp_deps = comp->get_deps();
            deps.insert(deps.end(), comp_deps.begin(), comp_deps.end());
        }
        return deps;
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
};

#endif