#pragma once
#include "uuid.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

// Cereal 序列化库依赖
#include <cereal/cereal.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

// 前置声明
class AssetManager;

// 使用 enum class 增强类型安全
enum class AssetType : uint8_t {
	unknown = 0,
	model,
	texture,
	shader,
	material,
	animation,
	scene,

	max_enum
};
namespace creal {

template <typename T>
concept AssetType = requires(T a) {
	{ a.on_load_asset() } -> std::same_as<void>;
};

template <typename T>
concept EntryType = requires(T a) {
	{ a.get_path() } -> std::same_as<void>;
};

template <typename Archive, AssetType T>
void SerializeAsset(Archive &ar, const char *name, std::shared_ptr<T> &asset) {
	ar(cereal::make_nvp(name, asset));

	if constexpr (Archive::is_loading::value) {
		if (asset) {
			asset->on_load_asset();
		}
	}
}

template <typename Archive, AssetType T>
void SerializeAssetArray(Archive &ar, const char *name, std::vector<std::shared_ptr<T>> &assets) {
	ar(cereal::make_nvp(name, assets));

	if constexpr (Archive::is_loading::value) {
		for (auto &asset : assets) {
			if (asset) {
				asset->on_load_asset();
			}
		}
	}
}

template <typename Archive, EntryType TEntry, typename TPath>
void SerializeFilePath(Archive &ar, const char *name, TEntry &entry, TPath &path) {
	if constexpr (Archive::is_saving::value) {
		if (entry) {
			path = entry->get_path();
		}
	}
	ar(cereal::make_nvp(name, path));
}
} //namespace creal
// ============================================================================
// AssetBinder
// 用于在 Load/Save 时处理 "资源引用 <-> UID" 的映射逻辑
// ============================================================================
class AssetBinder {
public:
	enum class Mode { load,
		save };

	AssetBinder(Mode mode, AssetManager *manager = nullptr) : mode_(mode), manager_(manager) {}

	// 通用绑定接口：自动推导是单个指针还是数组
	template <typename T>
	void bind(std::string_view name, T &field) {
		if (mode_ == Mode::load) {
			load_internal(name, field);
		} else {
			save_internal(name, field);
		}
	}

	// 获取序列化所需的数据 (供 AssetManager 使用)
	const auto &get_asset_map() const { return asset_map_; }
	const auto &get_asset_array_map() const { return asset_array_map_; }

	// 注入数据 (供 AssetManager 在反序列化时使用)
	void set_data(std::unordered_map<std::string, UID> &&map,
			std::unordered_map<std::string, std::vector<UID>> &&array_map) {
		asset_map_ = std::move(map);
		asset_array_map_ = std::move(array_map);
	}

private:
	Mode mode_;
	AssetManager *manager_ = nullptr;

	std::unordered_map<std::string, UID> asset_map_;
	std::unordered_map<std::string, std::vector<UID>> asset_array_map_;

	// --- Internal Helpers (Implementation omitted for brevity, assumes Manager logic) ---
	// 实际项目中，这里会调用 manager_->get_or_load_asset<T>(uid)

	template <typename T>
	void load_internal(std::string_view name, std::shared_ptr<T> &ptr);

	template <typename T>
	void load_internal(std::string_view name, std::vector<std::shared_ptr<T>> &vec);

	template <typename T>
	void save_internal(std::string_view name, const std::shared_ptr<T> &ptr) {
		asset_map_.emplace(name, ptr ? ptr->get_uid() : UID::Empty());
	}

	template <typename T>
	void save_internal(std::string_view name, const std::vector<std::shared_ptr<T>> &vec) {
		std::vector<UID> uids;
		uids.reserve(vec.size());
		for (const auto &p : vec) {
			uids.push_back(p ? p->get_uid() : UID::Empty());
		}
		asset_array_map_.emplace(name, std::move(uids));
	}
};

// ============================================================================
// Asset Base Class
// ============================================================================
class Asset : public std::enable_shared_from_this<Asset> {
public:
	Asset() = default;
	virtual ~Asset() = default;

	// --- Public Interfaces (snake_case) ---

	virtual std::string get_asset_type_name() const { return "Unknown"; }
	virtual AssetType get_asset_type() const { return AssetType::unknown; }

	// 生命周期回调
	virtual void on_load_asset() {}
	virtual void on_save_asset() {}

	/**
	 * @brief 核心引用绑定函数
	 * 子类需重写此函数，通过 binder 注册自己引用的其他资源
	 * 示例: binder.bind("albedo_map", albedo_texture_);
	 */
	virtual void bind_refs(AssetBinder &binder) {}

	// Getter
	inline const UID &get_uid() const { return uid_; }

	// Setter (通常仅在创建或导入时使用)
	void set_uid(const UID &id) { uid_ = id; }

protected:
	UID uid_ = {};

	friend class AssetManager;
	friend class cereal::access;

	// --- Serialization ---
	template <class Archive>
	void serialize(Archive &ar) {
		ar(cereal::make_nvp("uid", uid_));

		// 注意：Asset 自身的成员变量(如 float intensity) 在这里序列化
		// 而引用的其他 Asset (如 Texture*) 通过 bind_refs 在 Manager 层处理
	}
};

// 定义智能指针别名
using AssetRef = std::shared_ptr<Asset>;