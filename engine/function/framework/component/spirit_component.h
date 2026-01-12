#ifndef SPIRIT_COMPONENT_H
#define SPIRIT_COMPONENT_H
#include "engine/function/asset/basic/png.h"
#include "engine/function/framework/component.h"
#include "engine/function/asset/asset_macros.h"

class SpiritComponent : public Component {
    CLASS_DEF(SpiritComponent, Component)
    public:
    ASSET_DEPS(
        (std::shared_ptr<PNGAsset>, texture)
    )

    	template <class Archive>
	void serialize(Archive &ar) {
        serialize_deps(ar);   
    }
};
CEREAL_REGISTER_TYPE(SpiritComponent);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, SpiritComponent);
#endif