#ifndef ASSET_MANAGER_H
#define ASSET_MANAGER_H

#include "engine/function/asset/asset.h"
#include "engine/function/asset/uid.h"
#include <filesystem>
#include <memory>
#include <string_view>
#include <mutex>
#include <future>
#include <unordered_map>
#include <optional>

class AssetManager {
public:
    AssetManager() = default;
    ~AssetManager(); // 需要处理未完成的 future

    void init(const std::filesystem::path &game_path);
    void tick();

    // --- Path Utilities ---
    std::optional<std::filesystem::path> get_virtual_path(const std::filesystem::path &real_path);
    std::optional<std::filesystem::path> get_physical_path(std::string_view virtual_path);
    bool is_virtual_path(std::string_view path) const;
    [[nodiscard]]
    std::vector<std::shared_future<AssetRef>> enqueue_load_task(UID uid);

    AssetRef load_asset_blocking(UID uid);

    AssetRef get_asset_immediate(UID uid);

    template <typename T>
    std::shared_ptr<T> load_asset(const UID &uid) {
        return std::dynamic_pointer_cast<T>(load_asset_blocking(uid));
    }

    template <typename T>
    std::shared_ptr<T> load_asset(const std::string &virtual_path) {
        auto uid = get_uid_by_path(virtual_path);
        return load_asset<T>(uid);
    }

    // --- Asset Management ---
    void save_asset(AssetRef asset, const std::string &virtual_path);
    void register_asset(AssetRef asset, const std::string &virtual_path);
    
    // 注册路径与 UID 的映射 (通常在 Scan 阶段调用)
    void register_path(UID uid, const std::string &physical_path_str);
    UID get_uid_by_path(const std::string &virtual_or_physical_path);

private:
    void scan_directory(const std::filesystem::path &dir_path);
    UID peek_uid_from_file(const std::filesystem::path &path);
    
    // 实际执行加载的 Worker 函数
    AssetRef perform_load_from_disk(UID uid, const std::filesystem::path& path);
    void perform_save_to_disk(AssetRef asset, const std::filesystem::path& path);
    AssetDeps peek_asset_deps(const std::filesystem::path& path);

    void collect_dependencies_recursive(UID uid, std::vector<UID>& sorted_uids, std::unordered_set<UID>& visited_uids);
    void collect_save_dependencies_recursive(AssetRef asset, std::vector<AssetRef>& sorted_assets, std::unordered_set<UID>& visited_uids);

    // load helper
    std::optional<std::shared_future<AssetRef>> check_pending_cache(UID uid);
private:
    std::unordered_map<UID, AssetRef> assets_;
    std::unordered_map<UID, std::shared_future<AssetRef>> pending_assets_;

    // 路径映射
    std::unordered_map<std::string, UID> path_to_uid_;
    std::unordered_map<UID, std::string> uid_to_path_;

    std::mutex asset_mutex_; // 保护上述所有 Map

public:
    std::filesystem::path engine_path_;
    std::filesystem::path game_path_;

    const std::filesystem::path virtual_game_path_ = "/Game/";
    const std::filesystem::path virtual_engine_path_ = "/Engine/";
};

#endif