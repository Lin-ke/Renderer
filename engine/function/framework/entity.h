#pragma once

#include <memory>
#include <string>
#include <algorithm>
#include "engine/function/framework/component.h"
#include "engine/function/asset/asset.h"

class Entity
{
    std::vector<ComponentPtr> components_;
    std::string name_;

    // Parent-child hierarchy
    Entity* parent_ = nullptr;
    std::vector<std::unique_ptr<Entity>> children_;
    
public:
    void set_name(const std::string& name) { name_ = name; }
    const std::string& get_name() const { return name_; }
    template<typename T, typename... Args>
    T* add_component(Args&&... args) {
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = component.get();
        ptr->set_owner(this);
        ptr->on_init();
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

    // ====== Parent-child hierarchy ======

    Entity* get_parent() const { return parent_; }

    const std::vector<std::unique_ptr<Entity>>& get_children() const { return children_; }

    bool has_children() const { return !children_.empty(); }

    /**
     * @brief Add a child entity. Takes ownership.
     * @return Raw pointer to the added child.
     */
    Entity* add_child(std::unique_ptr<Entity> child) {
        child->parent_ = this;
        Entity* ptr = child.get();
        children_.push_back(std::move(child));
        return ptr;
    }

    /**
     * @brief Create a new child entity with the given name.
     * @return Raw pointer to the created child.
     */
    Entity* create_child(const std::string& child_name = "") {
        auto child = std::make_unique<Entity>();
        child->set_name(child_name);
        return add_child(std::move(child));
    }

    /**
     * @brief Remove a child entity. Returns the unique_ptr (ownership transferred to caller).
     */
    std::unique_ptr<Entity> remove_child(Entity* child) {
        for (auto it = children_.begin(); it != children_.end(); ++it) {
            if (it->get() == child) {
                child->parent_ = nullptr;
                auto result = std::move(*it);
                children_.erase(it);
                return result;
            }
        }
        return nullptr;
    }

    /**
     * @brief Detach this entity from its parent. Returns the unique_ptr.
     */
    std::unique_ptr<Entity> detach_from_parent() {
        if (parent_) {
            return parent_->remove_child(this);
        }
        return nullptr;
    }

    /**
     * @brief Reparent this entity under a new parent.
     *        Must be a root entity in the scene, or already detached.
     */
    void set_parent(Entity* new_parent) {
        if (parent_ == new_parent) return;
        if (parent_) {
            auto self = parent_->remove_child(this);
            if (new_parent) {
                new_parent->add_child(std::move(self));
            }
        } else {
            parent_ = new_parent;
        }
    }

    // ====== Serialization ======

    template<class Archive>
    void serialize(Archive &ar) {
        ar(cereal::make_nvp("name", name_));
        ar(cereal::make_nvp("components", components_));
        ar(cereal::make_nvp("children", children_));
    }

    /**
     * @brief Restore parent_ pointers after deserialization.
     *        Must be called on root entities after loading from archive.
     */
    void restore_hierarchy() {
        for (auto& child : children_) {
            if (child) {
                child->parent_ = this;
                child->restore_hierarchy();
            }
        }
    }

    // ====== Asset dependency traversal (recursive) ======

    void traverse_deps(std::function<void(std::shared_ptr<Asset>)> callback) const {
        for (const auto& comp : components_) {
            comp->traverse_deps(callback);
        }
        for (const auto& child : children_) {
            child->traverse_deps(callback);
        }
    }

    void load_asset_deps() {
        for (auto& comp : components_) {
            comp->load_asset_deps();
        }
        for (auto& child : children_) {
            child->load_asset_deps();
        }
    }

    void save_asset_deps() {
        for (auto& comp : components_) {
            comp->save_asset_deps();
        }
        for (auto& child : children_) {
            child->save_asset_deps();
        }
    }

    /**
     * @brief Tick all components, then recursively tick children.
     * @param delta_time Time since last frame in seconds
     */
    void tick(float delta_time) {
        for (auto& comp : components_) {
            if (comp) {
                comp->on_update(delta_time);
            }
        }
        for (auto& child : children_) {
            if (child) {
                child->tick(delta_time);
            }
        }
    }

    // Deep clone using reflection
    std::unique_ptr<Entity> clone();
};
