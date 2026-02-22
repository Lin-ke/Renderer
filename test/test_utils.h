#pragma once

#include <string>
#include <filesystem>
#include <regex>

namespace test_utils {

/**
 * @brief Check if a filename is a UUID format
 * UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
 */
inline bool is_uuid_filename(const std::string& filename) {
    // Remove extension
    std::string name = filename;
    size_t dot_pos = name.find_last_of('.');
    if (dot_pos != std::string::npos) {
        name = name.substr(0, dot_pos);
    }
    
    // Check UUID pattern: 8-4-4-4-12 hex digits
    std::regex uuid_pattern("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return std::regex_match(name, uuid_pattern);
}

/**
 * @brief Clean up UUID-named asset files in a directory
 * 
 * This function removes auto-generated asset files with UUID names,
 * while preserving human-readable named files.
 * 
 * @param directory The directory to clean up
 * @param recursive Whether to clean subdirectories recursively
 * @return Number of files deleted
 */
inline size_t cleanup_uuid_named_assets(const std::filesystem::path& directory, bool recursive = true) {
    size_t deleted_count = 0;
    
    if (!std::filesystem::exists(directory)) {
        return 0;
    }
    
    auto process_entry = [&](const std::filesystem::path& path) {
        if (!std::filesystem::is_regular_file(path)) {
            return;
        }
        
        std::string ext = path.extension().string();
        // Only process asset files
        if (ext != ".asset" && ext != ".binasset") {
            return;
        }
        
        std::string filename = path.filename().string();
        if (is_uuid_filename(filename)) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            if (!ec) {
                deleted_count++;
            }
        }
    };
    
    if (recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            process_entry(entry.path());
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            process_entry(entry.path());
        }
    }
    
    return deleted_count;
}

/**
 * @brief RAII helper to clean up UUID files on scope exit
 * 
 * Usage:
 *   auto cleanup = test_utils::ScopedAssetCleanup(test_asset_dir);
 *   // ... run test ...
 *   // files are auto-deleted when cleanup goes out of scope
 */
class ScopedAssetCleanup {
public:
    explicit ScopedAssetCleanup(std::filesystem::path directory) 
        : directory_(std::move(directory)) {}
    
    ~ScopedAssetCleanup() {
        cleanup_uuid_named_assets(directory_, true);
    }
    
    // Explicit cleanup before destruction
    size_t cleanup_now() {
        return cleanup_uuid_named_assets(directory_, true);
    }
    
    // Prevent copying
    ScopedAssetCleanup(const ScopedAssetCleanup&) = delete;
    ScopedAssetCleanup& operator=(const ScopedAssetCleanup&) = delete;
    
    // Allow moving
    ScopedAssetCleanup(ScopedAssetCleanup&&) = default;
    ScopedAssetCleanup& operator=(ScopedAssetCleanup&&) = default;
    
private:
    std::filesystem::path directory_;
};

} // namespace test_utils
