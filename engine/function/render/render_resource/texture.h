#pragma once

#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/function/asset/asset.h"
#include "engine/core/math/extent.h"
#include "engine/core/reflect/math_reflect.h"
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <cereal/access.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>

enum class TextureType {
    Texture2D = 0,
    Texture2DArray,
    TextureCube,
    Texture3D,
    MaxEnum
};

class Texture : public Asset {
public:
    Texture(const std::string& path);
    Texture(const std::vector<std::string>& paths, TextureType type);
    Texture(TextureType type, RHIFormat format, Extent3D extent, uint32_t array_layer = 1, uint32_t mip_levels = 0);
    ~Texture();

    virtual std::string_view get_asset_type_name() const override { return "Texture Asset"; }
    virtual AssetType get_asset_type() const override { return AssetType::Texture; }

    virtual void on_load_asset() override;

    TextureType get_texture_type() { return texture_type_; }
    const std::string& get_name() const { return name_; }
    void set_name(const std::string& name);

    RHITextureRef texture_;
    RHITextureViewRef texture_view_;

    void set_data(void* data, uint32_t size);

    uint32_t texture_id_ = 0;

    friend class cereal::access;
    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::base_class<Asset>(this));
        ar(cereal::make_nvp("name", name_));
        ar(cereal::make_nvp("paths", paths_));
        ar(cereal::make_nvp("texture_type", texture_type_));
        ar(cereal::make_nvp("format", format_));
        ar(cereal::make_nvp("extent", extent_));
        ar(cereal::make_nvp("mip_levels", mip_levels_));
        ar(cereal::make_nvp("array_layer", array_layer_));
    }

protected:
    std::string name_ = "";
    std::vector<std::string> paths_;
    TextureType texture_type_;
    RHIFormat format_;
    Extent3D extent_;
    uint32_t mip_levels_;
    uint32_t array_layer_;

    void init_rhi();
    void load_from_file();

private:
    Texture() = default;
};
using TextureRef = std::shared_ptr<Texture>;
