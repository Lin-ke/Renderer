#include "asset_manager.h"
#include "engine/core/log/Log.h"
#include "engine/function/asset/asset.h"

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <format> // C++20

namespace fs = std::filesystem;
enum class AssetFormat {
    Invalid = 0,
    Json,
    Binary
};
// 辅助：扩展名判断 (使用 string_view 避免拷贝)
static AssetFormat get_format(const fs::path& path) {
    // fs::path::extension() 返回的是 path 对象，这里转为 string 比较通用
    // 实际优化中可以将 extension 缓存或使用 string_view 比较
    auto ext = path.extension().string(); 
    if (ext == ".asset") return AssetFormat::Json;
    if (ext == ".binasset") return AssetFormat::Binary;
    return AssetFormat::Invalid;
}

void AssetManager::init(const fs::path& game_path) {
    EngineBasePath = fs::absolute(EngineBasePath); 
    EnginePath = EngineBasePath / "engine/resource";
    
    if (game_path.empty()) {
        GameBasePath = EngineBasePath;
        GamePath = EnginePath; // 假设 EnginePath 也是 fs::path 类型
    } else {
        GameBasePath = fs::absolute(game_path);
        // 使用 / 运算符自动处理路径分隔符
        GamePath = GameBasePath / "asset"; 
    }
}

void AssetManager::tick() {
}

bool AssetManager::is_virtual_path(std::string_view path) const {
    if (path.starts_with(EnginePath.string())) return true;
    if (!GamePath.empty() && path.starts_with(GamePath.string())) return true;
    return false;
}

AssetRef AssetManager::get_or_load_asset_internal(const UID& uid) {
    if (uid.is_empty()) return nullptr;
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
        const auto& path = it->second;
        if (fs::exists(path)) {
            return load_asset_from_disk(path, true);
        }
    }
    
    return nullptr;
}

AssetRef AssetManager::get_or_load_asset_internal(const std::string& path_str) {
    fs::path path(path_str);
    fs::path physical_path = get_physical_path(path); // 假设此函数也已改为 fs::path

    if (physical_path.empty() || !fs::exists(physical_path)) {
        return nullptr;
    }
    
    // 规范化路径字符串用于查找 (generic_string 处理 / 和 \ 的统一)
    std::string lookup_path = physical_path.generic_string();

    if (auto it = path_to_uid_.find(lookup_path); it != path_to_uid_.end()) {
        return get_or_load_asset_internal(it->second);
    }

    // unknown asset
    return nullptr;
}

// 优化：返回 fs::path，逻辑更清晰
fs::path AssetManager::get_virtual_path(const fs::path& real_path) {
    std::string real_path_str = real_path.generic_string(); 

    if (real_path_str.starts_with(GameBasePath.string())) {
        return GamePath / fs::relative(real_path, GameBasePath);
    } 
    else if (real_path_str.starts_with(EngineBasePath.string())) {
        return EnginePath / fs::relative(real_path, EngineBasePath);
    }
    ERR("Path {} is not under engine or game base path!", real_path_str);
    return {};
}
AssetRef AssetManager::load_asset_from_disk(const fs::path& physical_path, bool init_now) {
    AssetRef asset = nullptr;
    auto format = get_format(physical_path);

    std::ifstream ifs(physical_path, std::ios::binary);
    if (!ifs.is_open()) return nullptr;

    try {
        if (format == AssetFormat::Binary) {
            cereal::BinaryInputArchive archive(ifs);
            archive(asset);
        } else if (format == AssetFormat::Json) {
            cereal::JSONInputArchive archive(ifs);
            archive(asset);
        }
    } catch (const std::exception& e) {
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
    register_asset(asset, get_virtual_path(physical_path).generic_string());

    if (init_now) {
        asset->on_load_asset();
        assets_[asset->get_uid()] = asset;
    } else {
        uninitialized_assets_[asset->get_uid()] = asset;
    }

    return asset;
}

void AssetManager::register_asset(AssetRef asset, const std::string& path) {
    UID uid = asset->get_uid();
    if (auto it = uid_to_path_.find(uid); it != uid_to_path_.end()) {
        const std::string& old_path = it->second;
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

fs::path AssetManager::get_physical_path(const fs::path& virtual_path) {
    if (!GamePath.empty()) {
        fs::path p = GamePath / virtual_path;
        if (fs::exists(p)) return p;
    }
    if (!EnginePath.empty()) {
        fs::path p = EnginePath / virtual_path;
        if (fs::exists(p)) return p;
    }
    return GamePath / virtual_path;
}

void AssetManager::save_asset(AssetRef asset, const std::string& path) {
    fs::path physical_path = get_physical_path(path);
    if (physical_path.has_parent_path() && !fs::exists(physical_path.parent_path())) {
        fs::create_directories(physical_path.parent_path());
    }

    asset->on_save_asset();
    AssetFormat format = get_format(physical_path);
    
    std::ofstream os(physical_path, std::ios::binary);
    if (!os.is_open()) {
        ERR("Failed to open file for saving: {}", physical_path.string());
        return;
    }

    try {
        if (format == AssetFormat::Binary) {
            cereal::BinaryOutputArchive archive(os);
            archive(asset);
        } else {
            cereal::JSONOutputArchive archive(os);
            archive(asset);
        }
    } catch (const std::exception& e) {
        ERR("Serialization error: {}", e.what());
    }

    register_asset(asset, get_virtual_path(physical_path).generic_string());
}