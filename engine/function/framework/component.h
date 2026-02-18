#ifndef COMPONENT_H
#define COMPONENT_H
#include "engine/core/reflect/serialize.h"
#include <memory>
#include <functional>
#include <string>
#include <vector>

#define CLASS_DEF(Type, Base) \
public: \
    virtual std::string_view get_component_type_name() const override { return #Type; } \
    friend class cereal::access; \
    using BaseClass = Base; \

class Component {
	// do nothing.
public:
	virtual ~Component() = default;

	virtual void on_init() {}  // Called when component is added to entity

	virtual void traverse_deps(std::function<void(std::shared_ptr<class Asset>)> callback) const {}

    // 使用 ClassDB 进行属性设置
    virtual bool set_property(const std::string& field_path, const std::string& value_json);
    virtual std::string get_property(const std::string& field_path) const;

    virtual std::string_view get_component_type_name() const { return "Component"; }

    // 通用反射序列化：自动保存非默认值的属性
    void serialize_modify(class cereal::JSONOutputArchive& ar);
    void serialize_modify(class cereal::JSONInputArchive& ar);
    
    // 二进制归档的重载
    void serialize_save(class cereal::BinaryOutputArchive& ar);
    void serialize_save(class cereal::BinaryInputArchive& ar);

	friend class cereal::access;
    
    // Explicit overloads for supported archives to ensure reflection is used
    void serialize(cereal::JSONOutputArchive& ar) { serialize_modify(ar); }
    void serialize(cereal::JSONInputArchive& ar) { serialize_modify(ar); }
    void serialize(cereal::BinaryOutputArchive& ar) { serialize_save(ar); }
    void serialize(cereal::BinaryInputArchive& ar) { serialize_save(ar); }

    // Fallback for other archives (do nothing)
	template <class Archive>
	void serialize(Archive &ar) {}

	virtual void load_asset_deps() {}
	virtual void save_asset_deps() {}

    void set_owner(class Entity* entity) { owner_ = entity; }
    class Entity* get_owner() const { return owner_; }

protected:
    class Entity* owner_ = nullptr;
};

using ComponentPtr = std::unique_ptr<Component>;
#endif