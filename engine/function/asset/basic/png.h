#pragma once

#include "engine/function/asset/asset.h"
#include "engine/core/reflect/serialize.h"
#include "engine/function/asset/asset_macros.h"
#include <memory>
#include <vector>

class PNGAsset : public Asset {
public:

	int width_ = 0;
	int height_ = 0;
	int channels_ = 0;

	std::vector<unsigned char> pixels_;

	std::string_view get_asset_type_name() const override { return "Texture2D"; }
	AssetType get_asset_type() const override { return AssetType::Texture; }

	friend class cereal::access;

	template <class Archive>
	void serialize(Archive &ar) {
		ar(cereal::base_class<Asset>(this));
		ar(CEREAL_NVP(width_), CEREAL_NVP(height_), CEREAL_NVP(channels_), CEREAL_NVP(pixels_));
	}
};
CEREAL_REGISTER_TYPE(PNGAsset);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, PNGAsset);