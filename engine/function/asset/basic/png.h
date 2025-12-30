#ifndef ENGINE_FUNCTION_ASSET_BASIC_PNG_H
#define ENGINE_FUNCTION_ASSET_BASIC_PNG_H
#include "engine/function/asset/asset.h"
#include "engine/function/asset/asset_handle.h"
#include "engine/core/reflect/serialize.h"
class PNGAsset : public Asset {
public:
    // dependencies
    std::vector<AssetHandle<Asset>> dep1;
    AssetHandle<PNGAsset> dep2;

    int width = 0;
    int height = 0;
    int channels = 0;
    
    std::vector<unsigned char> pixels;

    // 返回资源类型名
    std::string_view get_asset_type_name() const override { return "Texture2D"; }
    AssetType get_asset_type() const override { return AssetType::Texture; }

    friend class cereal::access;
    template<class Archive>
    void serialize(Archive& archive) {
        archive(cereal::base_class<Asset>(this));
        
        archive(CEREAL_NVP(width), 
                CEREAL_NVP(height), 
                CEREAL_NVP(channels));

        archive(CEREAL_NVP(pixels)); 
        archive(CEREAL_NVP(dep1));
        archive(CEREAL_NVP(dep2));
    }
};
CEREAL_REGISTER_TYPE(PNGAsset);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, PNGAsset);
#endif