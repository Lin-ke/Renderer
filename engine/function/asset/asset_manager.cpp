#include "asset_manager.h"
#include "asset.h"
#include "engine/core/log/log.h"
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <filesystem>
#include <fstream>
namespace fs = std::filesystem;
static constexpr size_t UID_BYTE_SIZE = sizeof(UID); // 二进制 UID 大小
static constexpr size_t UID_STR_SIZE = 36;           // 字符串 UID 大小

void AssetManager::init(const fs::path& game_asset_root) {
    // make sure clean
    asset_cache_.clear();
    uninitialized_assets_.clear();
    uid_path_map.clear();
    path_uid_map.clear();
    
    for (auto& asset_path : {engine_asset_path_, game_asset_root / game_asset_dir_}) {
        if (fs::exists(asset_path)) {
        scan_directory(asset_path);
        } else {
            WARN("Asset path not found: {}", asset_path.string());
        }
    
    }
}

void AssetManager::scan_directory(const fs::path& directory) {
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension();
            if (ext == ".asset" || ext == ".binasset") {
                UID uid = peek_uid_from_file(entry.path());
                
                if (!uid.is_empty()) {
                    register_path(uid, entry.path());
                }
            }
        }
    }
}

UID AssetManager::peek_uid_from_file(const fs::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return UID::empty(); // Return empty UID

    std::string ext = path.extension().string();
    UID uid = UID::empty();
    if (ext == ".binasset") {
        ifs.read(reinterpret_cast<char*>(&uid), UID_BYTE_SIZE);
        if (ifs.gcount() != UID_BYTE_SIZE) return UID::empty();
        return uid;
    } else if (ext == ".asset") {
        char uid_str[UID_STR_SIZE + 1] = {0};
        ifs.read(uid_str, UID_STR_SIZE);
        if (ifs.gcount() != UID_STR_SIZE) return UID::empty();
        return UID(std::string(uid_str));
    }
    
    return UID::empty();
}


AssetRef AssetManager::get_or_load_internal(const fs::path &raw_path) {
	fs::path clean_path = fs::weakly_canonical(raw_path);

	UID uid = path_to_uid(clean_path);
	if (!uid.is_empty()) {
		return get_or_load_internal(uid);
	}
	return load_from_disk(clean_path, false);
}


AssetRef AssetManager::get_or_load_internal(const UID &uid) {
	if (asset_cache_.contains(uid)) {
		return asset_cache_[uid];
	}

	if (auto it = uninitialized_assets_.find(uid); it != uninitialized_assets_.end()) {
		AssetRef asset = it->second;
		// move to asset_cache_
		uninitialized_assets_.erase(it);
		asset_cache_[uid] = asset;
		asset->on_load(); 
		return asset;
	}

	auto path_opt = uid_to_path(uid);
	if (path_opt) {
		return load_from_disk(*path_opt, false);
	}

	return nullptr;
}

AssetRef AssetManager::load_from_disk(const fs::path &path, bool lazy) {
	fs::path clean_path = fs::weakly_canonical(path);

	if (!fs::exists(clean_path) || !fs::is_regular_file(clean_path)) {
		INFO("Asset not found at path: {}", clean_path.string());
		return nullptr;
	}

	std::ifstream ifs(clean_path, std::ios::binary);
	if (!ifs.is_open()) {
		return nullptr;
	}
	AssetRef asset = nullptr;
	std::string ext = clean_path.extension().string();
	if (ext == ".binasset") {
        ifs.seekg(UID_BYTE_SIZE, std::ios::cur);
		cereal::BinaryInputArchive archive(ifs);
		archive(asset); // 这里利用 Cereal 的多态支持，自动创建子类实例
	} else if (ext == ".asset") {
        char buffer[UID_STR_SIZE + 1]; // +1 for skipping newline
            ifs.read(buffer, UID_STR_SIZE); 
            char next = ifs.peek();
            if (next == '\n') ifs.ignore(1);
            else if (next == '\r') {
                ifs.ignore(1);
                if (ifs.peek() == '\n') ifs.ignore(1);
            }
		cereal::JSONInputArchive archive(ifs);
		archive(asset);
	} else {
		WARN("Unknown asset format: {}", ext);
		return nullptr;
	}

	if (!asset) {
		return nullptr;
	}

	const UID &uid = asset->get_uid();
	register_path(uid, clean_path);
	if (!lazy) {
		asset_cache_[uid] = asset;
		asset->on_load();
	} else {
		uninitialized_assets_[uid] = asset;
	}
	return asset;
}

void AssetManager::register_path(const UID &uid, const fs::path &path) {
	if (auto it = uid_path_map.find(uid); it != uid_path_map.end()) {
		const fs::path &old_path = it->second;

		if (old_path == path) {
			return;
		}
		path_uid_map.erase(old_path);
	}
	if (auto it = path_uid_map.find(path); it != path_uid_map.end()) {
		const auto &old_uid = it->second;
		uid_path_map.erase(old_uid);
	}

	uid_path_map.insert_or_assign(uid, path);
	path_uid_map.insert_or_assign(path, uid);
}

void AssetManager::save_asset(const AssetRef& asset, const fs::path& override_path) {
    if (!asset) return;

    asset->on_save(); // 准备序列化数据

    fs::path save_path = override_path;
    if (save_path.empty()) {
        auto existing = uid_to_path(asset->get_uid());
        if (existing) save_path = *existing;
        else {
             save_path = fs::path("Temp") / (asset->get_uid().to_string() + ".asset");
        }
    }
    fs::create_directories(save_path.parent_path());

    std::ofstream ofs(save_path, std::ios::binary);
    if (save_path.extension() == ".binasset") {
        UID uid = asset->get_uid();
        ofs.write(reinterpret_cast<const char*>(&uid), UID_BYTE_SIZE);
        cereal::BinaryOutputArchive archive(ofs);
        archive(asset);
    } else {
        UID uid = asset->get_uid();
        ofs.write(uid.to_string().c_str(), UID_STR_SIZE); 
        ofs.put('\n');
        cereal::JSONOutputArchive archive(ofs);
        archive(asset); 
    }

    register_path( asset->get_uid(),save_path);
    if (!asset_cache_.contains(asset->get_uid())) {
        asset_cache_[asset->get_uid()] = asset;
    }
}

void AssetManager::tick() {
    // cleanup unused assets
    std::erase_if(asset_cache_, [](const auto& item) {
        auto& [uid, asset_ptr] = item;
        if (asset_ptr.use_count() == 1) {
            INFO("Releasing unused asset: {}", uid.to_string());
            return true; 
        }
        return false; 
    });
}
