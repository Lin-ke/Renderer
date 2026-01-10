#ifndef COMPONENT_H
#define COMPONENT_H
#include "engine/core/reflect/serialize.h"
#include <memory>

class Component {
	// do nothing.
public:
	virtual ~Component() = default;

	virtual std::vector<std::shared_ptr<class Asset>> get_deps() const {
		return {};
	}

	friend class cereal::access;
	template <class Archive>
	void serialize(Archive &ar) {}

	virtual void load_asset_deps() {}
	virtual void save_asset_deps() {}
};

using ComponentPtr = std::unique_ptr<Component>;
#endif