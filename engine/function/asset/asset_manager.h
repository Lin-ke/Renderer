#ifndef ASSET_MANAGER_H
#define ASSET_MANAGER_H
#include "asset.h"
#include "uid.h"
#include <memory>
#include <string_view>
#include <filesystem>
class AssetManager {
public:
	AssetManager() = default;
	void init(const std::filesystem::path& game_path);
	void tick();
    std::filesystem::path get_virtual_path(const std::filesystem::path& real_path);
    std::filesystem::path get_physical_path(const std::filesystem::path& virtual_path);
    bool is_virtual_path(std::string_view path) const;

	template <typename T>
	std::shared_ptr<T> get_or_load_asset(const UID &uid) {
        auto asset = get_or_load_asset_internal(uid);
        return std::dynamic_pointer_cast<T>(asset);
	}
    template <typename T>
    std::shared_ptr<T> get_or_load_asset(const std::string& virtual_path) {
        auto asset = get_or_load_asset_internal(virtual_path);
        return std::dynamic_pointer_cast<T>(asset);
    }

    AssetRef get_or_load_asset_internal(const UID& uid);
    AssetRef get_or_load_asset_internal(const std::string& virtual_path);
    AssetRef load_asset_from_disk(const std::filesystem::path& path, bool init_now);
    void save_asset(AssetRef asset, const std::string& path);

    void register_asset(AssetRef asset, const std::string& virtual_path);
private:
	std::unordered_map<UID, AssetRef> assets_;
	std::unordered_map<UID, AssetRef> uninitialized_assets_;
    // 这里存物理的path
	std::unordered_map<std::string, UID> path_to_uid_;
	std::unordered_map<UID, std::string> uid_to_path_;

public:
    std::filesystem::path EngineBasePath;
    std::filesystem::path GameBasePath;
    std::filesystem::path EnginePath;
    std::filesystem::path GamePath;

};
#endif