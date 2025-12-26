#ifndef ASSET_MANAGER_H
#define ASSET_MANAGER_H
#include "uid.h"
#include "asset.h"
#include "engine/core/log/log.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>

class AssetManager{
public:
    AssetManager() = default;
    inline static const std::filesystem::path engine_root_ = ENGINE_ROOT;
    inline static const std::filesystem::path engine_asset_path_ = engine_root_ / "assets";
    inline static const std::filesystem::path game_asset_dir_ = "assets";

    void init(const std::filesystem::path& game_asset_root = "");
    void tick();
    void shutdown();
    UID path_to_uid(const std::filesystem::path& path);
    std::optional<std::filesystem::path> uid_to_path(const UID& uid);


    template<typename T>
    std::shared_ptr<T> get_or_load_asset(const UID& uid){
        AssetRef asset = get_or_load_internal(uid);
        if (asset) {
            auto casted = std::dynamic_pointer_cast<T>(asset);
            if (!casted) {
                ERR("Asset Type Mismatch! UID: {}", uid.to_string());
                return nullptr;
            }
            return casted;
        }
        return nullptr;
    }

    AssetRef get_or_load_internal(const std::filesystem::path& raw_path);
    AssetRef get_or_load_internal(const UID& uid);
    void register_path(const UID& uid, const std::filesystem::path& path);
    AssetRef load_from_disk(const std::filesystem::path& path, bool lazy = false);
    void save_asset(const AssetRef& asset, const std::filesystem::path& override_path = "");
    
    
private:
    // resource cache
    std::unordered_map<UID, AssetRef> asset_cache_;
    // avoid cycle
    std::unordered_map<UID, AssetRef> uninitialized_assets_;
    // path_uid map
    std::unordered_map<UID, std::filesystem::path> uid_path_map;
    std::unordered_map<std::filesystem::path, UID> path_uid_map;

    void scan_directory(const std::filesystem::path& directory);
    UID peek_uid_from_file(const std::filesystem::path& path);
    
};

#endif