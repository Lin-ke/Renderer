#pragma once

#include "engine/function/asset/asset.h"
#include "engine/function/asset/uid.h"
#include "engine/core/log/Log.h" // Include for DECLARE_LOG_TAG
#include <filesystem>
#include <memory>
#include <string_view>
#include <mutex>
#include <future>
#include <unordered_map>
#include <optional>

DECLARE_LOG_TAG(LogAsset);

/**
 * @brief Manages the lifecycle, loading, and saving of game assets.
 * 
 * The AssetManager handles:
 * - **Path Resolution**: converting between Virtual Paths (e.g., "/Game/Textures/T_Hero") and Physical Paths (e.g., "C:/Project/Content/Textures/T_Hero.asset").
 * - **Caching**: ensuring assets are loaded only once and reused (via weak/strong references, currently strong).
 * - **Async Loading**: loading assets in background threads to avoid stalling the main thread.
 * - **Dependencies**: automatically resolving and loading asset dependencies.
 */
class AssetManager {
public:
    AssetManager() = default;
    ~AssetManager();

    /**
     * @brief Initializes the asset manager with the project's root content directory.
     * Scans the directory to build the UID-to-Path mapping.
     */
    void init(const std::filesystem::path &game_path);
    
    /**
     * @brief Periodic update function (e.g., for garbage collection or task management).
     */
    void tick();

    // --- Path Utilities ---

    /**
     * @brief Converts a physical filesystem path to a virtual engine path.
     * @return std::nullopt if the path is not within the managed directories.
     */
    std::optional<std::filesystem::path> get_virtual_path(const std::filesystem::path &real_path);

    /**
     * @brief Converts a virtual engine path to a physical filesystem path.
     * @return std::nullopt if the virtual path is invalid.
     */
    std::optional<std::filesystem::path> get_physical_path(std::string_view virtual_path);

    /**
     * @brief Checks if a path string is formatted as a virtual path (starts with /Game or /Engine).
     */
    bool is_virtual_path(std::string_view path) const;

    /**
     * @brief Enqueues an asynchronous load task for an asset and its dependencies.
     * @return A vector of futures for the requested asset and its dependencies.
     */
    [[nodiscard]]
    std::vector<std::shared_future<AssetRef>> enqueue_load_task(UID uid);

    /**
     * @brief Loads an asset synchronously (blocking the current thread).
     * If the asset is already loaded or loading, returns the existing instance or waits for it.
     */
    AssetRef load_asset_blocking(UID uid);

    /**
     * @brief Retrieves an asset if it is already loaded; otherwise returns nullptr.
     * Does not trigger a load.
     */
    AssetRef get_asset_immediate(UID uid);

    /**
     * @brief Type-safe wrapper for asynchronous/blocking load by UID.
     * Currently implemented as blocking for simplicity, but designed to support async.
     */
    template <typename T>
    std::shared_ptr<T> load_asset(const UID &uid) {
        return std::dynamic_pointer_cast<T>(load_asset_blocking(uid));
    }

    /**
     * @brief Type-safe wrapper for loading by virtual path.
     */
    template <typename T>
    std::shared_ptr<T> load_asset(const std::string &virtual_path) {
        auto uid = get_uid_by_path(virtual_path);
        return load_asset<T>(uid);
    }

    // --- Asset Management ---

    /**
     * @brief Saves an asset to disk at the specified virtual path.
     * Serializes the asset and its metadata.
     */
    void save_asset(AssetRef asset, const std::string &virtual_path);

    /**
     * @brief Registers an asset in the manager without saving it to disk immediately.
     */
    void register_asset(AssetRef asset, const std::string &virtual_path);
    
    /**
     * @brief Manually registers a UID-to-Path mapping.
     * Typically called during the directory scan phase.
     */
    void register_path(UID uid, const std::string &physical_path_str);

    /**
     * @brief Look up the UID associated with a given path (virtual or physical).
     */
    UID get_uid_by_path(const std::string &virtual_or_physical_path);

private:
    /**
     * @brief Recursively scans a directory for asset files and registers them.
     */
    void scan_directory(const std::filesystem::path &dir_path);

    /**
     * @brief Reads the UID from an asset file header without fully deserializing it.
     */
    UID peek_uid_from_file(const std::filesystem::path &path);
    
    // --- Internal Workers ---

    /**
     * @brief Performs the actual deserialization from disk.
     */
    AssetRef perform_load_from_disk(UID uid, const std::filesystem::path& path);

    /**
     * @brief Performs the actual serialization to disk.
     */
    void perform_save_to_disk(AssetRef asset, const std::filesystem::path& path);

    /**
     * @brief Reads asset dependencies from the file header.
     */
    AssetDeps peek_asset_deps(const std::filesystem::path& path);

    // Recursive collectors for dependencies
    void collect_dependencies_recursive(UID uid, std::vector<UID>& sorted_uids, std::unordered_set<UID>& visited_uids);
    void collect_save_dependencies_recursive(AssetRef asset, std::vector<AssetRef>& sorted_assets, 
        std::unordered_set<UID>& visited_uids, std::unordered_set<UID>& visiting);

    // Helper to check if a load is already in progress
    std::optional<std::shared_future<AssetRef>> check_pending_cache(UID uid);

private:
    std::unordered_map<UID, AssetRef> assets_; ///< Cache of loaded assets.
    std::unordered_map<UID, std::shared_future<AssetRef>> pending_assets_; ///< Assets currently being loaded.

    // Path Mappings
    std::unordered_map<std::string, UID> path_to_uid_;
    std::unordered_map<UID, std::string> uid_to_path_;

    std::mutex asset_mutex_; ///< Protects all maps.

public:
    std::filesystem::path engine_path_;
    std::filesystem::path game_path_;

    const std::filesystem::path virtual_game_path_ = "/Game/";
    const std::filesystem::path virtual_engine_path_ = "/Engine/";
};