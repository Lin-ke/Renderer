#pragma once

#include <string>
#include <filesystem>
#include <regex>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

#include <stb_image_write.h>

// Forward declarations
class Scene;
class CameraComponent;

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

/**
 * @brief Save screenshot data to PNG file
 * @param filename Output file path
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param data Raw RGBA pixel data
 * @return true on success
 */
inline bool save_screenshot_png(const std::string& filename, uint32_t width, uint32_t height, const std::vector<uint8_t>& data) {
    int stride_in_bytes = width * 4;
    int result = stbi_write_png(filename.c_str(), static_cast<int>(width), static_cast<int>(height), 4, data.data(), stride_in_bytes);
    return result != 0;
}

/**
 * @brief Calculate average brightness of image
 * @param data Raw RGBA pixel data
 * @return Average brightness (0-255), or 0 if data is empty
 */
inline float calculate_average_brightness(const std::vector<uint8_t>& data) {
    if (data.empty()) return 0.0f;
    
    uint64_t total = 0;
    size_t pixel_count = data.size() / 4;
    
    for (size_t i = 0; i < pixel_count; i++) {
        uint32_t r = data[i * 4 + 0];
        uint32_t g = data[i * 4 + 1];
        uint32_t b = data[i * 4 + 2];
        total += (r + g + b) / 3;
    }
    
    return static_cast<float>(total) / static_cast<float>(pixel_count);
}

/**
 * @brief Result of scene loading operation
 */
struct SceneLoadResult {
    std::shared_ptr<Scene> scene;
    CameraComponent* camera = nullptr;
    bool success = false;
    std::string error_msg;
    
    bool is_valid() const { return success && scene && camera; }
};

/**
 * @brief Utility class for loading test scenes with proper initialization
 * 
 * Handles the common pattern of:
 * 1. Resolving virtual path to physical path
 * 2. Checking file existence
 * 3. Looking up UID from path
 * 4. Loading scene with dependencies
 * 5. Initializing components
 * 6. Finding camera and setting up render
 */
class SceneLoader {
public:
    /**
     * @brief Load a scene from virtual path with full initialization
     * @param virtual_path Virtual path like "/Game/scene.asset"
     * @param set_active Whether to set as active scene in World
     * @return SceneLoadResult with scene, camera, and status
     */
    static SceneLoadResult load(const std::string& virtual_path, 
                                 bool set_active = true);
    
    /**
     * @brief Check if scene file exists at virtual path
     */
    static bool scene_exists(const std::string& virtual_path);
};

/**
 * @brief Utility for render tests handling scene creation and rendering
 */
class RenderTestApp {
public:
    using SceneCreateFunc = std::function<bool(const std::string&)>;
    using SceneLoadedFunc = std::function<void(SceneLoadResult&)>;

    struct Config {
        std::string scene_path;
        uint32_t width = 1280;
        uint32_t height = 720;
        int max_frames = 60;
        int capture_frame = 30; // 0 to disable
        SceneCreateFunc create_scene_func = nullptr;
        SceneLoadedFunc on_scene_loaded_func = nullptr;
    };

    static bool run(const Config& config, std::vector<uint8_t>& out_screenshot_data, int* out_frames = nullptr);
    static bool capture_screenshot(std::vector<uint8_t>& screenshot_data);
};

/**
 * @brief Global test context for managing Engine lifecycle across tests
 * 
 * This allows tests to share a single Engine instance, significantly
 * reducing test execution time by avoiding repeated init/exit cycles.
 */
class TestContext {
public:
    /**
     * @brief Initialize the global Engine (called once before all tests)
     */
    static void init_engine();
    
    /**
     * @brief Shutdown the global Engine (called once after all tests)
     */
    static void shutdown_engine();
    
    /**
     * @brief Reset test state between tests without restarting Engine
     * 
     * This clears:
     * - Active scene
     * - Mesh renderer registrations
     * - Render batches
     * - Other per-test runtime state
     */
    static void reset();
    
    /**
     * @brief Check if Engine is initialized
     */
    static bool is_initialized() { return engine_initialized_; }

private:
    static bool engine_initialized_;
    static std::string test_asset_dir_;
};

} // namespace test_utils
