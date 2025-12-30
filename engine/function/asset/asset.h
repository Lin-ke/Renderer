#ifndef ASSET_H
#define ASSET_H

#include <memory>
#include <string_view>
#include <cereal/cereal.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/types/polymorphic.hpp>

#include "uid.h"

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
    virtual void on_load_asset() {} // 反序列化后，实际加载显存等重资源
    virtual void on_save_asset() {} // 序列化前调用

    const UID& get_uid() const { return uid_; }
    void set_uid(const UID& id) { uid_ = id; } // 允许管理器设置 UID

protected:
    UID uid_ = {};

    friend class AssetManager;
    friend class cereal::access;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("uid", uid_));
    }

};

using AssetRef = std::shared_ptr<Asset>;

#endif