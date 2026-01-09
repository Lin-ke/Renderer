#ifndef ASSET_H
#define ASSET_H

#include <memory>
#include <string_view>
#include <cereal/cereal.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/types/vector.hpp>
#include "uid.h"
#include <mutex>
// 资源类型枚举
enum class AssetType : uint8_t {
    Unknown = 0,
    Model,
    Texture,
    Shader,
    Material,
    Animation,
    Scene,
    MaxEnum
};

class AssetManager;

// template<typename T>
// concept AssetDerived = std::is_base_of_v<class Asset, T>;

class Asset : public std::enable_shared_from_this<Asset> {
public:
    virtual ~Asset() = default;
    virtual std::string_view get_asset_type_name() const { return "Unknown"; }
    virtual AssetType get_asset_type() const { return AssetType::Unknown; }
    virtual void on_load() {} 
    virtual void on_save() {} 
    virtual std::vector<std::shared_ptr<Asset>> get_deps() const { return {}; }
    
    const UID& get_uid() const { return uid_; }
    void set_uid(const UID& id) { uid_ = id; } // 允许管理器设置 UID
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

    virtual void load_asset_deps() {}
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

struct AssetDeps{
    std::vector<UID> deps_uid;
    friend class cereal::access;
    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("deps_uid", deps_uid));
    }
};

struct AssetDepsHelper {
    template <typename T>
    static void collect(std::vector<UID>& uids, const std::shared_ptr<T>& ptr) {
        if (ptr) uids.push_back(ptr->get_uid());
    }
    template <typename T>
    static void collect(std::vector<UID>& uids, const std::vector<std::shared_ptr<T>>& vec) {
        for (const auto& ptr : vec) {
            if (ptr) uids.push_back(ptr->get_uid());
        }
    }
};
using AssetRef = std::shared_ptr<Asset>;

#endif