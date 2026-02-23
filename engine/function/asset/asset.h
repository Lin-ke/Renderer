#pragma once

#include <memory>
#include <string_view>
#include <cereal/cereal.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/types/vector.hpp>
#include "uid.h"
#include <mutex>
enum class AssetType : uint8_t {
    Unknown = 0,
    Model,
    ModelCache,
    Mesh,
    Texture,
    Shader,
    Material,
    Animation,
    Scene,
    Prefab,
    MaxEnum
};

class AssetManager;

#include <functional>

// template<typename T>
// concept AssetDerived = std::is_base_of_v<class Asset, T>;

class Asset : public std::enable_shared_from_this<Asset> {
public:
    virtual ~Asset() = default;
    virtual std::string_view get_asset_type_name() const { return "Unknown"; }
    virtual AssetType get_asset_type() const { return AssetType::Unknown; }
    virtual void on_load() {} 
    virtual void on_save() {} 
    virtual void traverse_deps(std::function<void(std::shared_ptr<Asset>)> callback) const {}
    
    const UID& get_uid() const { return uid_; }
    void set_uid(const UID& id) { uid_ = id; }
    bool is_initialized() const { return initialized_.load(); }
    void mark_initialized() { initialized_.store(true); }
    
    bool is_dirty() const { return dirty_; }
    void mark_dirty() { dirty_ = true; }
    void clear_dirty() { dirty_ = false; }

protected:
    UID uid_ = {};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> dirty_{true}; // 默认为脏，新建资源需要保存
    std::mutex init_mutex_;
    friend class AssetManager;
    friend class cereal::access;
    
    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("uid", uid_));  
    }
    // load_asset_deps用于在加载完成后自动通过UID加载依赖
    virtual void load_asset_deps() {}
    
    // save_asset_deps用于把依赖的UID保存到内部的deps_uid变量中
    virtual void save_asset_deps() {}

private:
    friend class AssetManager;
    virtual void on_load_asset() { // asset recall
        load_asset_deps();
        on_load();
        clear_dirty();
    } 

    virtual void on_save_asset() {
        save_asset_deps();
        on_save();
    } 

};

using AssetDeps = std::vector<UID>;
using AssetRef = std::shared_ptr<Asset>;