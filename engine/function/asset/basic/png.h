#ifndef ENGINE_FUNCTION_ASSET_BASIC_PNG_H
#define ENGINE_FUNCTION_ASSET_BASIC_PNG_H
#include "engine/function/asset/asset.h"
#include "engine/core/reflect/serialize.h"
#include "engine/function/asset/asset_macros.h"
#include <memory>
#include <vector>

class PNGAsset : public Asset {
public:
	ASSET_DEPS(
			(std::vector<std::shared_ptr<Asset>>, dep1),
			(std::shared_ptr<PNGAsset>, dep2))

	int width = 0;
	int height = 0;
	int channels = 0;

	std::vector<unsigned char> pixels;

	// 返回资源类型名
	std::string_view get_asset_type_name() const override { return "Texture2D"; }
	AssetType get_asset_type() const override { return AssetType::Texture; }

	friend class cereal::access;

	template <class Archive>
	void serialize(Archive &ar) {
		ar(cereal::base_class<Asset>(this));
		ar(CEREAL_NVP(width), CEREAL_NVP(height), CEREAL_NVP(channels), CEREAL_NVP(pixels));
		serialize_deps(ar);
	}
};
CEREAL_REGISTER_TYPE(PNGAsset);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, PNGAsset);
#endif