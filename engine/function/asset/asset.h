#ifndef ASSET_H
#define ASSET_H
#include "uid.h"

#include <memory>
#include <string_view>
#include <concepts>

#include <cereal/types/memory.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/access.hpp>

#include "engine/main/engine_context.h"

enum class AssetType {
    Unknown = 0,
    Model,
    Texture,
    Shader,
    Material,
    Animation,
    Scene,
    MaxEnum
};

class AssetManager; // Forward declaration

// Concept: 约束必须是 Asset 的子类
template<typename T>
concept IsAsset = std::derived_from<T, class Asset>;


class Asset : public std::enable_shared_from_this<Asset> {
public:
    Asset() = default;
    virtual ~Asset() = default;

    virtual std::string_view get_type_name() const { return "Unknown"; }
    virtual AssetType get_type() const { return AssetType::Unknown; }

    // 生命周期回调
    virtual void on_load() {} 
    virtual void on_save() {}

    const UID& get_uid() const { return uid_; }
    void set_uid(const UID& id) { uid_ = id; } // 允许管理器设置UID

protected:
    UID uid_ = {};

    // 移除宏，通过友元让 Manager 处理复杂逻辑，或者让 AssetManager 提供 Resolve 方法
    friend class AssetManager;
    friend class cereal::access;

private:
    // Cereal 序列化
    template <class Archive>
    void serialize(Archive& ar) {
        ar(CEREAL_NVP(uid_));
    }
};




#endif