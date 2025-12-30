#include "asset_manager.h"
#include "engine/core/log/Log.h"
#include "engine/function/asset/asset.h"

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <filesystem>
#include <format> // C++20
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
enum class AssetFormat {
	Invalid = 0,
	Json,
	Binary
};
static AssetFormat get_format(const fs::path &path) {
	auto ext = path.extension().string();
	if (ext == ".asset") {
		return AssetFormat::Json;
	}
	if (ext == ".binasset") {
		return AssetFormat::Binary;
	}
	return AssetFormat::Invalid;
}

void AssetManager::init(const fs::path &game_path) {
	std::filesystem::path engine_base_path_ = fs::absolute(ENGINE_PATH);
	engine_path_ = engine_base_path_ / "assets";

	if (game_path.empty()) {
		game_path_ = engine_path_;
	} else {
		game_path_ = fs::absolute(game_path) / "assets";
	}
	// INFO("AM at: epath, {}, gpath, {}", engine_path_.string(), game_path_.string());
	for (auto &asset_path : { engine_path_, game_path_ }) {
		if (fs::exists(asset_path)) {
			scan_directory(asset_path);
		} else {
			fs::create_directories(asset_path);
			INFO("Created asset directory at {}", asset_path.string());
		}
	}
}

void AssetManager::scan_directory(const fs::path &dir_path) {
	for (const auto &entry : fs::recursive_directory_iterator(dir_path)) {
		if (!entry.is_regular_file() || get_format(entry.path()) == AssetFormat::Invalid) {
			continue;
		}
		UID uid = peek_uid_from_file(entry.path());
		if (!uid.is_empty()) {
			register_path(uid, entry.path().generic_string());
		}
	}
}

UID AssetManager::peek_uid_from_file(const fs::path &path) {
	std::ifstream ifs(path, std::ios::binary);
	if (!ifs.is_open()) {
		return UID::empty(); // Return empty UID
	}
	UID uid;
	switch (get_format(path)) {
		case AssetFormat::Binary: {
			uid.read<true>(ifs);
			break;
		}
		case AssetFormat::Json: {
			uid.read<false>(ifs);
			break;
		}
		default:
			uid = UID::empty();
			break;
	}
	return uid;
}

void AssetManager::tick() {
}

bool AssetManager::is_virtual_path(std::string_view path) const {
	if (path.starts_with(engine_path_.string())) {
		return true;
	}
	if (!game_path_.empty() && path.starts_with(game_path_.string())) {
		return true;
	}
	return false;
}

AssetRef AssetManager::get_or_load_asset_internal(const UID &uid) {
	if (uid.is_empty()) {
		return nullptr;
	}
	if (auto it = assets_.find(uid); it != assets_.end()) {
		return it->second;
	}
	if (auto it = uninitialized_assets_.find(uid); it != uninitialized_assets_.end()) {
		AssetRef asset = std::move(it->second); // 移动语义
		uninitialized_assets_.erase(it);

		asset->on_load_asset();
		assets_.emplace(uid, asset); // emplace 稍微高效一点
		return asset;
	}
	if (auto it = uid_to_path_.find(uid); it != uid_to_path_.end()) {
		const auto &path = it->second;
		if (fs::exists(path)) {
			return load_asset_from_disk(path, true);
		}
	}

	return nullptr;
}

AssetRef AssetManager::get_or_load_asset_internal(const std::string &path_str) {
	fs::path path(path_str);
	auto physical_path = get_physical_path(path);

	if (!physical_path.has_value() || !fs::exists(physical_path.value())) {
		return nullptr;
	}

	// 规范化路径字符串用于查找 (generic_string 处理 / 和 \ 的统一)
	std::string lookup_path = physical_path->generic_string();

	if (auto it = path_to_uid_.find(lookup_path); it != path_to_uid_.end()) {
		return get_or_load_asset_internal(it->second);
	}

	// unknown asset
	return nullptr;
}

AssetRef AssetManager::load_asset_from_disk(const fs::path &physical_path, bool init_now) {
	AssetRef asset = nullptr;
	auto format = get_format(physical_path);

	std::ifstream ifs(physical_path, std::ios::binary);
	if (!ifs.is_open()) {
		return nullptr;
	}

	try {
		UID uid;
		if (format == AssetFormat::Binary) {
			uid.read<true>(ifs);
			cereal::BinaryInputArchive archive(ifs);
			archive(asset);
		} else if (format == AssetFormat::Json) {
			uid.read<false>(ifs);
			cereal::JSONInputArchive archive(ifs);
			archive(asset);
		}
	} catch (const std::exception &e) {
		ERR("Deserialization error for {}: {}", physical_path.string(), e.what());
		return nullptr;
	}

	if (!asset) {
		ERR("Failed to load asset from path {}", physical_path.string());
		return nullptr;
	}

	if (asset->get_uid().is_empty()) {
		// 警告：这里通常不应该随意生成 UID，除非是新资源导入流程
		// 如果是读取现有资源但 UID 为空，通常意味着数据损坏
		asset->set_uid(UID());
		return nullptr;
	}

	// 更新索引
	register_asset(asset, physical_path.generic_string());

	if (init_now) {
		asset->on_load_asset();
		assets_[asset->get_uid()] = asset;
	} else {
		uninitialized_assets_[asset->get_uid()] = asset;
	}

	return asset;
}

void AssetManager::register_path(UID uid, const std::string &path) {
	if (auto it = uid_to_path_.find(uid); it != uid_to_path_.end()) {
		const std::string &old_path = it->second;
		if (old_path != path) {
			path_to_uid_.erase(old_path);
		}
	}

	if (auto it = path_to_uid_.find(path); it != path_to_uid_.end()) {
		UID old_uid = it->second;
		if (old_uid != uid) {
			uid_to_path_.erase(old_uid);
		}
	}

	uid_to_path_[uid] = path;
	path_to_uid_[path] = uid;
}

void AssetManager::register_asset(AssetRef asset, const std::string &path) {
	UID uid = asset->get_uid();
	register_path(uid, path);
}

std::optional<fs::path> AssetManager::get_physical_path(const fs::path &virtual_path) {
	if (virtual_path.string().starts_with(virtual_game_path_.string())) {
		fs::path relative_path = fs::relative(virtual_path, virtual_game_path_);
		return game_path_ / relative_path;
	} else if (virtual_path.string().starts_with(virtual_engine_path_.string())) {
		fs::path relative_path = fs::relative(virtual_path, virtual_engine_path_);
		return engine_path_ / relative_path;
	}
	return std::nullopt;
}

std::optional<fs::path> AssetManager::get_virtual_path(const fs::path &real_path) {
	std::string real_path_str = real_path.generic_string();
	if (real_path_str.starts_with(engine_path_.generic_string())) {
		fs::path relative_path = fs::relative(real_path, engine_path_);
		return virtual_engine_path_ / relative_path;
	} else if (real_path_str.starts_with(game_path_.generic_string())) {
		fs::path relative_path = fs::relative(real_path, game_path_);
		return virtual_game_path_ / relative_path;
	}
	return std::nullopt;
}

void AssetManager::save_asset(AssetRef asset, const std::string &path) {
	std::optional<fs::path> physical_path = get_physical_path(path);
	if (!physical_path.has_value()) {
		ERR("Invalid virtual path for saving asset: {}", path);
		return;
	}

	if (physical_path->has_parent_path() && !fs::exists(physical_path->parent_path())) {
		fs::create_directories(physical_path->parent_path());
	}

	asset->on_save_asset();
	AssetFormat format = get_format(physical_path.value());
	std::ofstream os(physical_path.value(), std::ios::binary);

	try {
		UID uid = asset->get_uid();
		if (format == AssetFormat::Binary) {
			uid.write<true>(os);
			cereal::BinaryOutputArchive archive(os);
			archive(asset);
		} else {
			uid.write<false>(os);
			cereal::JSONOutputArchive archive(os);
			archive(asset);
		}
	} catch (const std::exception &e) {
		ERR("Serialization error: {}", e.what());
	}

	register_asset(asset, physical_path.value().generic_string());
}